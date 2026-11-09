// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)

#include <unistd.h>
#include <sys/socket.h>
#define _CRT_SECURE_NO_WARNINGS    // stops VS code from showing warnings for functions like fopen , scanf and other
#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <sstream> 
#include <stdexcept>
using namespace std;

// int32_t is used for 32-bit signed integers, int64_t is used for 64-bit signed integers
// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2;          // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024;      // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *top;
    int32_t count;

public:
    // Implement these functions:
    Stack()
    {
		top = nullptr;
		count = 0;
    }
    void push(const T &val)
    {
		Node* temp = new Node;
		temp->data = val;
		temp->next = top;   
		top = temp;
		count++;

    }
    T pop()
    {
        if (isEmpty())
        {
			throw runtime_error("Stack is empty");
        }
		Node* temp = top;
		T ans = top->data;
		top = top->next;
		delete temp;
		count--;
		return ans;
    }
    T &peek()
    {
		if (isEmpty())
		{
	        throw runtime_error("Stack is empty");
		}
		return top->data;
    }
    bool isEmpty()
    {
		return top == nullptr;
    }
    int32_t depth()
    {
		return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
		Node* temp = top;
		int32_t i = 0;
        while (temp != nullptr && i < maxLen)
        {
			out[i] = temp->data;
			temp = temp->next;
			i++;
        }
        return i;
    }
};


// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    
    Timeline()  
    {
		head = nullptr;
		tail = nullptr;
		stepCount = 0;
    }
    void record(Snapshot *s)
    {
		if (s == nullptr)
		{
            return;
		}
        TimelineNode* temp = new TimelineNode;
		temp->data = s; 
		temp->next = nullptr;
		temp->prev = tail;

		if (head == nullptr)
		{
			head = temp;
			tail = temp;
		}
		else
		{
			tail->next = temp;
			tail = temp;
		}
        stepCount++;
    }
    TimelineNode *begin()
    {
		return head;
    }
    int32_t getStepCount()
    {
		return stepCount;
    }
};

// Core structs
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};


// resolve.bin - bookkeeping
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



// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream& in, string& out)  // in this we are reading the source line from the file
{
	while (getline(in, out))
	{
		if (!out.empty() && out.back() == '\r') 
		{
			out.pop_back();
		}

		if (!out.empty()) 
		{
			return true;
		}
	}
	return false; 
}
string firstWord(const string& line) // in this we are getting the first word from the line
{
	stringstream word(line);
	string first;
	word >> first;
	return first;
}
string secondWord(const string& line) // in this we are getting the second word from the line
{
	stringstream word(line);
    string first;
    string second;
    word >> first;
    word >> second;
	return second;
}
bool validateProgram(const char* sourcePath) // in this we are validating the program by checking the func and func_end
{
    ifstream fin(sourcePath, ios::binary); 

	if (!fin.is_open())
	{
        cout << "error failed to open file " << endl;
		return false;
	}

	string line;
	bool temp = false;
	int countline = 0;
	while (readSourceLine(fin, line))
	{
		countline++;
		string first = firstWord(line);
        if (first == "func")
        {
            if (temp==true)
            {
                cout << "error at line " << countline << ": nested func not allowed" << endl;
                return false;
            }
            temp = true;
        }
        else if (first == "func_end")
        {
            if (temp==false)
            {
                cout << "error at line " << countline << ": func_end without matching func" << endl;
                return false;
            }
            temp = false;
        }
    }

    if (temp == true)
    {
		cout << "error at line " << countline << ": func without matching func_end" << endl;
		return false;
    }

	return true;
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE* f, int64_t offsetField, const string& text) // in this we are writing the resolve record to the file
{
	int64_t pos = ftell(f);       // get the current position in the file ftell returns the current file position of the given stream
	fwrite(&offsetField, sizeof(int64_t), 1, f);  // fwrite parameters: (pointer to data, size of each element, number of elements, file pointer)
	int32_t size = text.size();
	fwrite(&size, sizeof(int32_t), 1, f);
	fwrite(text.c_str(), 1, size, f);  
	return pos;


}
int64_t readResolveRecord(FILE* f, string& outText) // in this we are reading the resolve record from the file
{
	int32_t size;
	int64_t offsetField;
	if (fread(&offsetField, sizeof(int64_t), 1, f) != 1) // fread parameters: (pointer to data, size of each element, number of elements, file pointer)
	{
		return -1;
	}
	if (fread(&size, sizeof(int32_t), 1, f) != 1)
	{
		return -1;
	}
	outText.resize(size);

	if (fread(&outText[0], 1, size, f) != size)
	{
		return -1;
	}
	return offsetField;
}
int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath) // in this we are resolving the program by reading the source file and writing the resolve file
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;
    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 

    ifstream fin(sourcePath, ios::binary);
    if (!fin.is_open())
    {
        cout << "error failed to open file " << endl;
        return -1;
    }


    FILE* fout = fopen(resolveBinPath, "wb");
    if (!fout)
    {
        cout << "error failed to open resolve file " << endl;
        return -1;
    }
	// read each line from source.bin, write to resolve.bin, and record FUNC or CALL info
    string line;
    while (readSourceLine(fin, line))
    {
        int64_t pos = ftell(fout);
        string word1 = firstWord(line);
        string word2 = secondWord(line);

        if (word1 == "func")
        {

            funcArray[funcCount].funcName = word2;
            funcArray[funcCount].byteOffsetInResolveBin = pos;
            funcCount++;
        }
        else if (word1 == "call")
        {
            patches[patchCount].byteOffsetOfOffsetField = pos;
            patches[patchCount].targetFuncName = word2;
            patchCount++;
        }
        writeResolveRecord(fout, pos, line);
    }

    fclose(fout);
	// now patch the CALL offsets with their target FUNC offsets
    FILE* f = fopen(resolveBinPath, "r+b");
    if (f == nullptr)
    {
        cout << "error failed to open resolve file for patching" << endl;
        return -1;
    }

	// for each patch, find the target functions offset and write it to the offset field
    for (int i = 0; i < patchCount; i++)
    {
        int64_t target = -1;
        for (int j = 0; j < funcCount; j++)
        {
            if (funcArray[j].funcName == patches[i].targetFuncName)
            {
                target = funcArray[j].byteOffsetInResolveBin;
                break;
            }
        }
        if (target == -1)
        {
            cout << "error unresolved function call to " << patches[i].targetFuncName << endl;
            fclose(f);
            return -1;
        }
        fseek(f, patches[i].byteOffsetOfOffsetField, SEEK_SET);
        fwrite(&target, sizeof(int64_t), 1, f);
    }
    fclose(f);
	// find the main function's offset and return it
    for (int i = 0; i < funcCount; i++)
    {
        if (funcArray[i].funcName == "main")
        {
            return funcArray[i].byteOffsetInResolveBin;
        }
    }
    cout << "error main function not found" << endl;

    return -1;
}
// PASS 0x2: EXECUTION (tokenization happens here) 
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string& line, Token tokens[], int32_t maxTokens) // in this we are tokenizing the line into tokens so that we can easily identify the instruction, identifier and params
{
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated

	stringstream ss(line);
	string word;
	int32_t count = 0;
	while (ss >> word && count < maxTokens)
	{
		if (count == 0)
		{
			tokens[count].type = KEYWORD;
		}
		else if (count == 1)
		{
			tokens[count].type = IDENTIFIER;
		}
		else
		{
			tokens[count].type = PARAM;
		}
		tokens[count].text = word;
		count++;
	}
	return count;
}


Snapshot* buildSnapshot(Stack<Frame>& callStack) // in this we are building the snapshot of the call stack so that we can record it in the timeline
{
    // build the snapshot based on the callStack given
	Snapshot* s = new Snapshot();
	s->stackDepth = callStack.snapshot_into(s->callStack, MAX_STACK_DEPTH);
	return s;
}

Variable* findVar(Frame& frame, const string& name) // in this we are finding the variable in the frame by searching in locals first and then in argv 
{
	// search in locals first
	for (int i = 0; i < frame.localCount; i++)
	{
		if (frame.locals[i].name == name)
		{
			return &frame.locals[i];
		}
	}
	// then search in argv
	for (int i = 0; i < frame.argc; i++)
	{
		if (frame.argv[i].name == name )
		{
			return &frame.argv[i];
		}
	}
	return nullptr;
}

int32_t getVarValue(Frame& frame, const string& name) // in this we are getting the value of the variable by searching in the frame and if not found then we are converting the name to integer and returning it
{
	Variable* v = findVar(frame, name);
	if (v!= nullptr)
	{
		return v->value;
	}
	return stoi(name); 
}
void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack

    // implementation:
    // execute line by line, and according to the keyword perform action

	FILE* f = fopen(resolveBinPath, "rb");   // open the resolve.bin file for reading  
    if (f == nullptr)
    {
        cout << "error failed to open resolve file for execution" << endl;
        return;
    }

	// first we need to create a call stack to keep track of the function calls and their local variables
    Stack<Frame> callStack;


    string argSource[MAX_STACK_DEPTH][MAX_VARS_PER_FRAME];

    Frame mainFrame;
    mainFrame.func_name = "main";
    mainFrame.argc = 0;
    mainFrame.returnLine = -1;
    mainFrame.localCount = 0;
    callStack.push(mainFrame);

    fseek(f, mainOffset, SEEK_SET);
    string line;

    readResolveRecord(f, line);
	// here below we are starting the execution of the program by reading each line from the resolve.bin file 
    while (true)
    {
        int64_t target = readResolveRecord(f, line);
        if (target == -1)
        {
            break;
        }

        Token tokens[MAX_TOKENS];
        int32_t count = tokenizeLine(line, tokens, MAX_TOKENS);
        string text = tokens[0].text;
        Frame& top = callStack.peek();

		// check the keyword and perform the action 
        if (text == "set")
        {
            Variable* v = findVar(top, tokens[1].text);
            if (v == nullptr)
            {
                v = &top.locals[top.localCount];
                v->name = tokens[1].text;
                top.localCount++;
            }
            v->value = getVarValue(top, tokens[2].text);
        }
        else if (text == "add" || text == "sub" || text == "mul" || text == "div")
        {
            Variable* v = findVar(top, tokens[1].text);
            int32_t val = getVarValue(top, tokens[2].text);

            if (text == "add")
            {
                v->value = v->value + val;
            }
            else if (text == "sub")
            {
                v->value = v->value - val;
            }
            else if (text == "mul")
            {
                v->value = v->value * val;
            }
            else if (text == "div")
            {
                if (val == 0)
                {
                    cout << "error division by zero in function " << endl;
                    break;;
                }
                v->value = v->value / val;
            }
        }
        else if (text == "call")
        {
            int32_t returnline = ftell(f);
            fseek(f, target, SEEK_SET);

            string funcline;
            readResolveRecord(f, funcline);
            Token functokens[MAX_TOKENS];
            int32_t funccount = tokenizeLine(funcline, functokens, MAX_TOKENS);
            int32_t argcount = count - 2;
            if (funccount - 2 != argcount)
            {
                cout << "error argument count mismatch in function call" << endl;
                break;
            }
			// here weare creating a new frame for the function call and pushing it onto the call stack
			//so that we can keep track of the function calls and their local variables
            Frame nframe;
            nframe.func_name = functokens[1].text;
            nframe.argc = argcount;
            nframe.returnLine = returnline;
            nframe.localCount = 0;
            int32_t index = callStack.depth();

            for (int i = 0; i < argcount; i++)
            {
                string argvalue = tokens[i + 2].text;
                string argname = functokens[i + 2].text;
                nframe.argv[i].name = argname;
                nframe.argv[i].value = getVarValue(top, argvalue);
                argSource[index][i] = argvalue;
            }
            callStack.push(nframe);
        }
        else if (text == "func_end")
        {
            if (callStack.depth() == 1)
            {
                break;
            }
            if (callStack.isEmpty())
            {
                cout << "error func_end without matching func" << endl;
                break;
            }
            int32_t index = callStack.depth() - 1;
            Frame finalFrame = callStack.pop();
            Frame& callerFrame = callStack.peek();
			// here we are updating the caller frame's variables with the values of the callee frame's variables
            for (int i = 0; i < finalFrame.argc; i++)
            {
                string callerName = argSource[index][i];
                int32_t argvalue = finalFrame.argv[i].value;
                Variable* v = findVar(callerFrame, callerName);
                if (v != nullptr)
                {
                    v->value = argvalue;
                }
            }
            fseek(f, finalFrame.returnLine, SEEK_SET);
        }
		// now we are building the snapshot of the call stack and recording it in the timeline
        timeline.record(buildSnapshot(callStack));

    }
    fclose(f);


}

// PASS 0x3: SERIALIZE TIMELINE

void writeHeader(FILE* f, const TTDBHeader& h) 
{
	fwrite(h.magic, 1, 4, f);
	fwrite(&h.version, sizeof(int32_t), 1, f);
	fwrite(&h.stepCount, sizeof(int32_t), 1, f);
	fwrite(&h.indexOffset, sizeof(int64_t), 1, f);
}

void writeString(FILE* f, const string& s) 
{
	int32_t size = s.size();
	fwrite(&size, sizeof(int32_t), 1, f);
	fwrite(s.c_str(), 1, size, f);
}
void writeVariable(FILE* f, const Variable& v)            
{
	writeString(f, v.name);
	fwrite(&v.value, sizeof(int32_t), 1, f);
}
// writeFrame writes a Frame to the file, including its function name, argument count, arguments, return line, local variable count, and local variables
// why? because we need to serialize the Frame structure to the file so that we can later reconstruct it when reading the file
void writeFrame(FILE* f, const Frame& frame) 
{
	writeString(f, frame.func_name);
	fwrite(&frame.argc, sizeof(int32_t), 1, f);
	for (int i = 0; i < frame.argc; i++)
	{
		writeVariable(f, frame.argv[i]);
	}
	fwrite(&frame.returnLine, sizeof(int32_t), 1, f);
	fwrite(&frame.localCount, sizeof(int32_t), 1, f);
	for (int i = 0; i < frame.localCount; i++)
	{
		writeVariable(f, frame.locals[i]);
	}
}
// writeSnapshot writes a Snapshot to the file, including its stack depth and the frames in the call stack
//why? because we need to serialize the Snapshot structure to the file so that we can later reconstruct it when reading the file
void  writeSnapshot(FILE* f, const Snapshot& snapshot)
{
	fwrite(&snapshot.stackDepth, sizeof(int32_t), 1, f);
	for (int i = 0; i < snapshot.stackDepth; i++)
	{
		writeFrame(f, snapshot.callStack[i]);
	}
}

// writeTdbg writes the Timeline to the tdbg file, including the header, snapshots, and index array
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header

	FILE* f = fopen(tdbgPath, "wb");
    if (f == nullptr)
    {
		cout << "error failed to open tdbg file for writing" << endl;
		return;
    }
	int32_t n = timeline.getStepCount();

    TTDBHeader h;
	h.magic[0] = 'T';
	h.magic[1] = 'T';
	h.magic[2] = 'D';
	h.magic[3] = 'B';
	h.version = 1;
	h.stepCount = n;
	h.indexOffset = 0; 
	writeHeader(f, h);


	int64_t* index = new int64_t[n];
	int32_t i = 0;
	for (TimelineNode* node = timeline.begin(); node != nullptr; node = node->next)
	{
		index[i] = ftell(f);
		writeSnapshot(f, *node->data);
		i++;
	}
	h.indexOffset = ftell(f);
	fwrite(index, sizeof(int64_t), n, f);

	fseek(f, 0, SEEK_SET);
	writeHeader(f, h);

	fclose(f);
	delete[] index;
}
// main section
int32_t main() // in this we are validating the source.bin file, resolving the program, executing the program and writing the timeline to the tdbg file
{

    if (!validateProgram("source.bin"))
    {
		cout << "error failed to validate source.bin" << endl;
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}


// for my understanding 
//frame and stack
// each functiion gets its own sheet for its local variables and arguments
//when a function is called, a new sheet is created on top and pushed onto the stack
//when work is done, the sheet is popped off and discarded

//snapshot and timeline
// snapshot is a copy of the whole pile of sheets at a given moment in time
//the photos are stored in a timeline, which is a doubly linked list of snapshots so we can move back and forth in time

//index
//photos have variable sizes, so we need an index to know where each photo starts in the file
// if we have 1000 snapshots, we need an array of 1000 offsets to know where each snapshot starts in the file
// the index is stored at the end of the file, and the header contains the offset of the index

//example 

// func foo b         main set k = 10
// set a 10           main calls foo with k   -> b =10
// add b a            foo sets a = 10 then b = 10+10 =20
// func_end           foo end  20 goes back to k  so k = 20
// func main
// set k = 10         5 photos taken saved in session.tdbg
//call foo k
//func_end

//small example and tell how it save the in.tdbg

// func main
// set k = 5
// add k 10
// func_end

// photo 1 
//after set k = 5    stack depth = 1, main frame, k=5
// photo 2
// after add k 10    stack depth = 1, main frame, k=15

// now file have header, 2 snapshots, index array of 2 offsets 

// writeTdbg function 

// 54 54 44 42 | 01 00 00 00 | 02 00 00 00 | 00 00 00 00 00 00 00 00 |   
// "TTDB"      | version 1 | stepCount = 2 | indexOffset = 0 (placeholder) |

// bytes 0 to 19 so file start at 20th byte


// write snapshot 1
// 01 00 00 00                     depth = 1 
// 04 00 00 00 6D 61 69 6E         func_name = "main" and size = 4
// 00 00 00 00                     argc = 0 no arguments
// FF FF FF FF                     returnLine = -1
// 01 00 00 00                     localCount = 1
// 01 00 00 00 6B                  var name = "k" and size = 1
// 05 00 00 00                     value = 5

// like that next snapshot 2 will be written and then index array will be written at the end of the file and then header will be updated with the index offse

// so the final file will look like this

//bytes       content        meaning
// 0-19        header          "TTDB" version 1 stepCount = 2 indexOffset = 0 (placeholder)
// 20-53       snapshot 1      depth = 1, main frame, k=5
// 54-85       snapshot 2      depth = 1, main frame, k=15
// 86-101      index array      offset of snapshot 1 = 20, offset of snapshot 2 = 54