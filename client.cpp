// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helper.h"
#include <sstream>
#include <sys/types.h>
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
const int IO_BUFFER_SIZE = 64 * 1024;     // fixed buffer for streaming files to and from the socket
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.0";
int LB_PORT = 5000;


// ---- Structs only the client needs ----
struct TdbgReader
{
    FILE* file;              // use standard C file pointer
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================

bool recvAll(int sock, char *buf, size_t n)
{
    // recv() can return fewer bytes than asked for, so keep calling it until exactly n bytes are read.
    // Return false if the connection closes or fails before n bytes arrive.
}
bool sendAll(int sock, const char *buf, size_t n)
{
    // send() can send fewer bytes than asked for, so keep calling it until all n bytes are sent.
    // Use the flag MSG_NOSIGNAL. Return false if the connection fails.
}

int connectToHost(const char *host, int port)
{
    // Create a TCP socket and connect it to host:port.
    // Return the socket, or -1 if the connection fails.
}
bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][CRC-32]. The CRC covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================

// hepler functions for reading the .tdbg file
bool read_exact(int fd, void *buf, size_t n)
{
    char *ptr = (char *)buf;
    size_t bytes_read = 0;
    while (bytes_read < n)
    {
        ssize_t result = read(fd, ptr + bytes_read, n - bytes_read);
        if (result <= 0)
        {
            return false;
        }
        bytes_read += result;
    }
    return true;
}
// helper function to read a string from a file
string read_string(FILE * f)
{
    int32_t size= 0;
    if (fread(&size, sizeof(int32_t), 1, f) != 1)
    {
        return "";
    }
    if (size < 0)
    {
        return "";
    }
    string s(size, '\0');
    fread(&s[0],1, size, f);
    return s;
}
bool openTdbg(const char *path, TdbgReader &r)
{
    r.file = fopen(path, "rb");
    if (!r.file) return false;

    // FIX: Read field-by-field to avoid struct padding issues!
    if (fread(r.header.magic, 1, 4, r.file) != 4) {
        fclose(r.file); r.file = nullptr; return false;
    }
    if (fread(&r.header.version, sizeof(int32_t), 1, r.file) != 1) {
        fclose(r.file); r.file = nullptr; return false;
    }
    if (fread(&r.header.stepCount, sizeof(int32_t), 1, r.file) != 1) {
        fclose(r.file); r.file = nullptr; return false;
    }
    if (fread(&r.header.indexOffset, sizeof(int64_t), 1, r.file) != 1) {
        fclose(r.file); r.file = nullptr; return false;
    }

    // check the magic
    if (r.header.magic[0] != 'T' || r.header.magic[1] != 'T' || r.header.magic[2] != 'D' || r.header.magic[3] != 'B')
    {
        fclose(r.file); r.file = nullptr; return false;
    }

    // seek to the index and read it
    r.index.resize(r.header.stepCount);
    if (fseek(r.file, r.header.indexOffset, SEEK_SET) != 0)
    {
        fclose(r.file); r.file = nullptr; return false;
    }
    
    if(fread(r.index.data(), sizeof(int64_t), r.header.stepCount, r.file) != (size_t)r.header.stepCount)
    {
        fclose(r.file); r.file = nullptr; return false;
    }
    return true;
}
void closeTdbg(TdbgReader &r)
{
    // 
    if (r.file)
    {
        fclose(r.file);
        r.file = nullptr;
    }
    r.index.clear();
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
 if (step < 0 || step >= r.header.stepCount)
    {
        return nullptr;
    }
    // seek to the snapshot
    if (fseek(r.file, r.index[step], SEEK_SET) != 0)
    {
        return nullptr;
    }
    // read the snapshot
    Snapshot *s = new Snapshot();
    if (fread(&s->stackDepth, sizeof(int32_t), 1, r.file) != 1)
    {
        delete s;
        return nullptr;
    }
    // read each frame
    for (int32_t i = 0; i < s->stackDepth; ++i)
    {
        Frame &f = s->callStack[i];
        f.func_name = read_string(r.file);
        fread(&f.argc, sizeof(int32_t), 1, r.file);
        for (int32_t j = 0; j < f.argc; ++j)
        {
            f.argv[j].name = read_string(r.file);
            fread(&f.argv[j].value, sizeof(int32_t), 1, r.file);
        }
        fread(&f.returnLine, sizeof(int32_t), 1, r.file);
        fread(&f.localCount, sizeof(int32_t), 1, r.file);
        for (int32_t j = 0; j < f.localCount; ++j)
        {
            f.locals[j].name = read_string(r.file);
            fread(&f.locals[j].value, sizeof(int32_t), 1, r.file);
        }

    }
    return s;
}
// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
    s.active_flag = 0; 
    s.reader.file = nullptr;
    s.currentStep = 0;
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
    if (s.active_flag)
    {
        closeTdbg(s.reader);
        s.active_flag = 0;
    }
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
    Snapshot * snap = readSnapshotAt(s.reader, s.currentStep);
    if (!snap)
    {
        std::cout << "damaged .tdbg file" << std::endl;
        return false;
    }
    printSnapshot(s.currentStep, s.reader.header.stepCount, *snap);
    delete snap;
    return true;
}
bool moveBy(DebugSession &s, int32_t delta)
{
    int32_t n = s.currentStep + delta;
    if (n < 0 )
    {
        std::cout << "already at the first step." << std::endl;
        return false;
    }
    if (n >= s.reader.header.stepCount)
    {
        std::cout << "already at the last step." << std::endl;
        return false;
    }
    s.currentStep = n;
    return showCurrent(s);
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
   string tdbgpath = "session.tdbg"; // hardcoded for testing
    TdbgReader new_read;
    if(!openTdbg(tdbgpath.c_str(), new_read))
    {
        std::cout << "server sent a damaged .tdbg file" << std::endl;
        return false;
    }
    if (s.active_flag)
    {
        closeTdbg(s.reader);    
    }
    s.reader = new_read;
    s.currentStep = 0;
    s.active_flag = 1;
    return showCurrent(s);
}
bool cmdNext(DebugSession &s) 
{
    return moveBy(s, 1);
}
bool cmdPrv(DebugSession &s) 
{
    return moveBy(s, -1);
}
bool cmdJmpNext(DebugSession &s, int32_t n) 
{
    return moveBy(s, n);
}
bool cmdJmpPrv(DebugSession &s, int32_t n) 
{
    return moveBy(s, -n);
}

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

// void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
// {

// }
void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
    std::cout << "\n[ Step " << step + 1 << " / " << totalSteps << " ]" << std::endl;
    for (int i = 0; i < s.stackDepth; i++) {
        const Frame& f = s.callStack[i];
        std::cout << "  Frame: " << f.func_name << " (";
        if (f.argc > 0) {
            std::cout << "Args: ";
            for (int a = 0; a < f.argc; a++) {
                std::cout << f.argv[a].name << "=" << f.argv[a].value << (a < f.argc - 1 ? ", " : "");
            }
            std::cout << " | ";
        }
        std::cout << "Locals: ";
        if (f.localCount == 0) std::cout << "none";
        for (int l = 0; l < f.localCount; l++) {
            std::cout << f.locals[l].name << "=" << f.locals[l].value << (l < f.localCount - 1 ? ", " : "");
        }
        std::cout << ")" << endl;
    }
}
void printHelp()
{
    std::cout << "Commands:" << endl;
    std::cout << "  start <file>     send a program to the server and open the result" << endl;
    std::cout << "  next             go one step forward" << endl;
    std::cout << "  prv              go one step back" << endl;
    std::cout << "  jmp next <n>     go n steps forward" << endl;
    std::cout << "  jmp prv <n>      go n steps back" << endl;
    std::cout << "  help             show this help" << endl;
    std::cout << "  quit             leave the debugger" << endl;
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
    
    stringstream ss(line);
    string cmd;
    ss >> cmd;

    if (cmd == "ttdb") ss >> cmd; 
    if (cmd.empty()) return true;

    if (cmd == "start") {
        string path;
        if (!(ss >> path)) { std::cout << "Usage: start <file>\n"; return false; }
        return cmdStart(s, path);
    }
    else if (cmd == "next") 
    {
        if (!s.active_flag) 
        { 
            std::cout << "No active session. Use 'start <file>' first.\n"; 
            return false; 
        }
        return cmdNext(s);
    }
    else if (cmd == "prv") 
    {
        if (!s.active_flag) 
        { 
            std::cout << "No active session. Use 'start <file>' first.\n"; 
            return false; 
        }
        return cmdPrv(s);
    }
    else if (cmd == "jmp") 
    {
        string dir;
        int32_t n;
        if (!(ss >> dir >> n) || n <= 0) 
        { 
            std::cout << "Usage: jmp <next|prv> <n>\n"; 
            return false; 
        }
        if (!s.active_flag) 
        { 
            std::cout << "No active session. Use 'start <file>' first.\n"; 
            return false; 
        }

        if (dir == "next")
        {
            return cmdJmpNext(s, n);
        }
        else if (dir == "prv")
        {
            return cmdJmpPrv(s, n);
        }
        else 
        {
            std::cout << "Usage: jmp <next|prv> <n>\n"; 
            return false; 
        }
    }
    else if (cmd == "help") 
    {
        printHelp();
        return true;
    }
    else if (cmd == "quit" || cmd == "exit") 
    {
        quitRequested = true;
        return true;
    }

    std::cout << "unknown command\n";
    printHelp();
    return false;

}
// int main(int argc, char *argv[])
// {
//     // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

//     // DebugSession session; initSession(session);
//     // Print a short welcome and the help text.
//     //
//     // Infinite loop:
//     //   if quitRequested, break
//     //
//     // After the loop: closeSession(session) and return 0.
//     // Never exit the loop because of an error. Only quit or the end of the input leaves it.

//     DebugSession session;
//     initSession(session);

//     cout << "TTDB (" << PROGRAM_NAME << ") - Time Travel Debugger" << endl;
//     printHelp();
//     bool quitRequested = false;
//     string line;
//     while (true)
//     {
//         if (quitRequested)
//         {
//             break;
//         }
//         cout << PROMPT;
//         if (!getline(cin, line))
//         {
//             break; // end of input
//         }
//         runCommand(session, line, quitRequested);
//     }
//     closeSession(session);
//     return 0;
// }
int main(int argc, char *argv[])
{
    DebugSession session; 
    initSession(session);
    
    std::cout << "TTDB (ttdb) - Time Travel Debugger\n";
    printHelp();

    bool quitRequested = false;
    string line;

    while (!quitRequested) {
        std::cout << PROMPT;
        if (!getline(std::cin, line)) break; // Handle Ctrl+D (End of file)
        runCommand(session, line, quitRequested);
    }

    closeSession(session);
    return 0;
}