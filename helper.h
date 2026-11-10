#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
using namespace std;

const int MAX_VARS_PER_FRAME = 16;
const int MAX_STACK_DEPTH = 64;

struct Variable {
    string name;
    int32_t value;
};

struct Frame {
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};

struct Snapshot {
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};

struct TTDBHeader {
    char magic[4];      // "TTDB"
    int32_t version;    // 1
    int32_t stepCount;  // total number of snapshots
    int64_t indexOffset;// where the index array starts in the file
};

// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};


// --- tiny CRC-32 used by every hop's trailer ---
// poly 0xEDB88320 (reflected), init 0xFFFFFFFF, final XOR 0xFFFFFFFF
static inline uint32_t crcTick(uint32_t a, unsigned char b)
{
    a ^= b;
    for (int k = 0; k < 8; k++)
    {
        a = (a & 1u) ? ((a >> 1) ^ 0xEDB88320u) : (a >> 1);
    }
    return a;
}

static inline uint32_t crcOf(const char *p, size_t n)
{
    uint32_t a = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
    {
        a = crcTick(a, (unsigned char)p[i]);
    } 
    return a ^ 0xFFFFFFFFu;
}

// Receiver: the body end is unknown until the stream stops, so we always
// delay the last 4 bytes. Whatever falls out of the ring is body; what is
// still inside at the end is the trailer.
class TailSeal
{
    uint32_t acc;
    unsigned char ring[4];
    int fill;

public:
    TailSeal() : acc(0xFFFFFFFFu), fill(0) {}

    void push(const char *p, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            unsigned char b = (unsigned char)p[i];
            if (fill < 4) 
            { 
                ring[fill++] = b; 
            }
            else
            {
                acc = crcTick(acc, ring[0]);
                ring[0] = ring[1];
                ring[1] = ring[2];
                ring[2] = ring[3];
                ring[3] = b;
            }
        }
    }

    bool ok() const
    {
        if (fill < 4) 
        {
            return false;
        }
        uint32_t got = acc ^ 0xFFFFFFFFu;
        uint32_t want =  (uint32_t)ring[0]
                      | ((uint32_t)ring[1] <<  8)
                      | ((uint32_t)ring[2] << 16)
                      | ((uint32_t)ring[3] << 24);
        return got == want;
    }
};


/*
how connection is establish 

[client]  -> (127.0.0.0 :5000)  ->
[LB]   ->(127.0.0.0:5001)  ->  server


1:  first client connect to the LB
    > in client.cpp in  main() and cmdstart()
    we uses connetiontohost(lbhost ,lbport) to establish
    a tcp connection to lb
    use the default address and port use hardcored in cpp
    string LB_HOST = "127.0.0.0";
    int LB_PORT = 5000;

2:  the LB accept the client  
    > in LB.cpp in main and acceptloop()
    we use LB creates a listening door on lbport
    using createlistensocket(lbport)and accept after alient connect
    as same port 5000

3: the LB connects to the server
    > in LB.cpp in relayoneclient() and  connecttoseverwwithwakeup()
    the lb tries to connet to the server using
    connecttoserver(serverhost, serverport) if fail it run system("./server 5001 &")
    use the default address and port use hardcored in cpp
    int lbPort = 5000;
    string serverHost = "127.0.0.0";
    int serverPort = 5001;

4:  the sever listen for the client 
    > in server.cpp in main()
    create a listening socket and binds it to pt(the port passed to it by the lb)
    usinf 5001 port which is passed by system() command



127.0.0.1 is the standard localhost address
127.0.0.0 is the network address for the loopback subnet

so i use the 127.0.0.1
*/