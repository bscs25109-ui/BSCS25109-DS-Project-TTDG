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