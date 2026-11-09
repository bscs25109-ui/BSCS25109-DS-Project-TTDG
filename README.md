# BSCS25109-Time-Travel-Debugger-DS-Project



04 October 2026





->Custom Stack<T> 
Implemented the linked-list stack which i commit





->I implemented source file reading and basic program validation for Pass 0x0 and also Commit and push to main.




->I implemented Pass 0x1 Resolve and function call patching and  Committed it also push to main



07 October 2026


->Verifies binary file saving and structural integrity using the [offset 8B][size 4B][text] data format

->Verified the binary output to ensure offsets, string sizes, and resolved function calls are stored correctly.

->implement Stage 03: Pass 0x2: Execution  then run in test.cpp and then verify it 



10 October 2026

-> Implemented Stage 04: Pass 0x3 Serialization. The Timeline is saved to session.tdbg
   as a 20-byte header (TTDB, version, stepCount, indexOffset), the snapshot stream,
   and a dense index of 8-byte offsets

12 October 2026


-> Verified session.tdbg in the hex view

-> Tested with the demo program and  Added comments to
   the whole code for my own understanding and pushed to main


9-11-2026   
-> Started Phase 2 and add structures (Frame, Snapshot, TTDBHeader) in helper.h to share data across Server, Client, and Load Balancer.
-> Implemented the custom Queue in load_balancer.cpp.
-> Cleaned up server.cpp to use helper.h and verified it still compiles.

-> added small CRC-32 helper (crcTick, crcOf) and
   TailSeal class in helper.h for the [status][body][crc32] traileron every hop. 


-> add functions of client.cpp part a 
   buffer -> TailSeal verify -> status0 = save body
   as .tdbg, status1 = return body as error
   on checksum failure,file is removed and error is "checksum mismatch, response corrupted".
 
 -> added functions to read TDBG data, load the file index, find specific steps, and read Snapshots while checking for invalid data.


 ->added command handling, input checks, and the main loop. Completed file upload/download with CRC checks and local time travel features.


 ->implemented LB socket helpers and  added sendAll (MSG_NOSIGNAL), sendError (with CRC-32 trailer), createListenSocket (SO_REUSEADDR, SOCK_CLOEXEC), and connectToServer.

->implemented server wakeup   ld.cpp part a

->implement lb.cpp completely