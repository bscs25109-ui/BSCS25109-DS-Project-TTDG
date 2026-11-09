#include <thread>
#include <fstream>
#include <iterator>
#define main client_real_main        // avoid two mains when we include client.cpp
#include "client.cpp"
#undef main

// minimal mock: accept one client, drain upload, send [status][body][crc32]
static void mockServer(int port, const string& body, int status, bool corrupt)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (sockaddr*)&a, sizeof(a)); listen(s, 1);

    int c = accept(s, nullptr, nullptr);
    char buf[4096]; while (recv(c, buf, sizeof(buf), 0) > 0) {}     // drain upload

    string full; full.push_back((char)status); full += body;
    uint32_t crc = crcOf(full.data(), full.size());
    if (corrupt) crc ^= 0xFFFFFFFFu;
    full.push_back((char)( crc        & 0xFF));
    full.push_back((char)((crc >>  8) & 0xFF));
    full.push_back((char)((crc >> 16) & 0xFF));
    full.push_back((char)((crc >> 24) & 0xFF));
    send(c, full.data(), full.size(), 0);
    shutdown(c, SHUT_WR);
    close(c); close(s);
}

int main()
{
    ofstream("src.bin") << "func main\nset x 1\nfunc_end\n";
    string err;

    // Case 1: valid reply, status 0
    thread t1(mockServer, 65001, "TDBG-body-data", 0, false);
    usleep(200000);
    int s1 = connectToHost("127.0.0.1", 65001);
    bool u1 = s1 >= 0 && uploadSource(s1, "src.bin");
    bool ok1 = u1 && receiveResponse(s1, "out1.tdbg", err);
    close(s1); t1.join();
    cout << "Case1 status0 valid : " << (ok1 ? "OK" : "FAIL") << "\n";

    // Case 2: valid reply, status 1 (server/LB error)
    err.clear();
    thread t2(mockServer, 65002, "backend said nope", 1, false);
    usleep(200000);
    int s2 = connectToHost("127.0.0.1", 65002);
    uploadSource(s2, "src.bin");
    bool ok2 = receiveResponse(s2, "out2.tdbg", err);
    close(s2); t2.join();
    cout << "Case2 status1 error : "
         << (!ok2 && err == "backend said nope" ? "OK" : "FAIL") << "\n";

    // Case 3: corrupted trailer -> must reject + delete file
    err.clear();
    thread t3(mockServer, 65003, "junk", 0, true);
    usleep(200000);
    int s3 = connectToHost("127.0.0.1", 65003);
    uploadSource(s3, "src.bin");
    bool ok3 = receiveResponse(s3, "out3.tdbg", err);
    close(s3); t3.join();
    cout << "Case3 corrupt      : "
         << (!ok3 && err == "checksum mismatch, response corrupted" ? "OK" : "FAIL") << "\n";
    cout << "  out3 removed?     : "
         << (access("out3.tdbg", F_OK) != 0 ? "yes (OK)" : "no (FAIL)") << "\n";
    return 0;
}