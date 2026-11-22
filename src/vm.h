#ifndef FAB_VM_H
#define FAB_VM_H

#include "string.h"
#include "threading.h"
#include "stream.h"
#include "error.h"

#include <stdint.h>
#include <stdbool.h>

#define VMLIST(T, N) {\
	IVLB(union {\
		T N;\
		size_t nextFree;\
	});\
	size_t firstFree;\
}

typedef struct VM VM;

typedef size_t VMID;
typedef size_t VMObjID;
typedef size_t VMLblID;
typedef size_t VMStreamID;

typedef unsigned VMScopeFlags;
enum {
	VMSCOPE_PROG  = 1U << 0,
	VMSCOPE_PC    = 1U << 1,
	VMSCOPE_FRAME = 1U << 2,
	VMSCOPE_VARS  = 1U << 3,
	VMSCOPE_ARGS  = 1U << 4,
	VMSCOPE_EXC   = 1U << 5,
	VMSCOPE_SRC   = 1U << 6,
	VMSCOPE__ALL  = 0xFFFF,
	VMSCOPE_OUTPUT   = 1U << 16,
	VMSCOPE_ARRCAT   = 1U << 17,
	VMSCOPE_USEFRAME = 1U << 18
};

typedef struct {
	unsigned nameOff;
	unsigned line;    // 0 for unknown
	unsigned col;     // 0 for end of line
	unsigned endLine; // 0 for none
	unsigned endCol;  // 0 for end of line
} VMSrcLoc;

typedef unsigned VMCallFlags;
enum {
	VMCALL_SILENT = 1U << 0, // Output will be discarded, use isRetTruthy instead
};
typedef FabErr (*VMCall)(VM*, VMCallFlags, size_t argc, VMObjID* argv, void* ctx, VMObjID* ret, int* isRetTruthy);

typedef enum {
	VMOP_EXIT,      // Exit current scope
	VMOP_SCOPE,     // Push a new scope (for locals and exceptions)
	VMOP_END,       // Pop the current scope
	VMOP_JMP,       // Jump
	VMOP_JNZ,       // Jump if last command output was true
	VMOP_JZ,        // Jump if last command output was false
	VMOP_PUSH,      // Create a new object on the stack
	VMOP_STR,       // Concat a string to the current object
	VMOP_VAR,       // Concat a variable to the current object
	VMOP_ARG,       // Concat an argument or the argument count to the current object
	VMOP_ARR,       // Create an array from elements in the current frame, then concat it
	VMOP_EXPAND,    // Expand an array
	VMOP_SETVAR,    // Set a variable
	VMOP_CALL,      // Call an external function
	VMOP_SETEXC,    // Set exception handler
	VMOP_RAISE,     // Raise an exception
	VMOP_SETSRC,    // Set source code info
} VMOpCode;
typedef struct {
	VMOpCode code;
	union {
		struct {
			VMScopeFlags flags;
		} scope;
		struct {
			VMLblID labelId;
		} jmp, jnz, jz;
		struct {
			unsigned arrCat : 1;
			size_t dataOff;      // String table offset
			size_t dataLen;
		} str;
		struct {
			unsigned arrCat : 1;
			size_t nameOff;
			uint32_t nameCrc;
		} var;
		struct {
			unsigned arrCat : 1;
			size_t index;        // -1 for argc ($?)
		} arg;
		struct {
			unsigned arrCat : 1;
		} arr;
		struct {
			unsigned append : 1;
			size_t nameOff;
			uint32_t nameCrc;
		} setVar;
		struct {
			VMCall ptr;
			void* ctx;
		} call;
		struct {
			VMLblID labelId;
		} setExc;
		struct {
			VMSrcLoc src;
		} setSrc;
	} opnds;
} VMOp;
typedef struct VLB(VMOp) VMOpBuf;
typedef struct {
	VMOp* inst;
	VMLblID* lblTbl;
	char* strTbl;
} VMProg;

typedef enum {
	VMOBJ_NULL,
	VMOBJ_STR,
	VMOBJ_ARR,
	VMOBJ_STREAM,
	VMOBJ_COW
} VMObjType;
typedef struct VMObj VMObj;
struct VMObj {
	VMObjType type;
	union {
		struct {
			CharBuf text;
			unsigned free : 1;
			unsigned dup : 1;
		} str;
		struct VLB(VMObjID) arr;
		VMStreamID stream;
		struct {
			VMObjID objId;
			size_t off;
			size_t len;
			size_t cowId;
		} cow;
	} data;
	size_t refCount;
	struct VMLIST(VMObjID, obj) cow;
};
typedef enum {
	VMOBJSTRSTOR_STATIC, // String is read-only but always available, CoW
	VMOBJSTRSTOR_XFER,   // Transfer ownership of string
	VMOBJSTRSTOR_DUP     // String will not always be available, dup immediately
} VMObjStrStorType;

typedef struct {
	VMObjID objId;
	char* name;
	uint32_t nameCrc;
} VMVar;

typedef struct {
	VMScopeFlags flags : 31;
	unsigned isLastOutTruthy : 1;
	unsigned isOutTruthy : 1;
	VMProg* prog;
	size_t pc;
	size_t frame;
	VMObjID out;
	struct VMLIST(VMVar, var) localVars;
	size_t argc;
	size_t argv;
	VMLblID excLblId;                    // Label of exception handler (-1 for none)
	VMSrcLoc src;
} VMScope;

typedef struct {
	IOStream stream;
	char* name;
	size_t refCount;
} VMStream;

struct VM {
	unsigned exit : 1;
	unsigned retVal : 1;
	struct VLB(VMScope) scopes;
	struct VLB(VMObjID) stack;
	struct VMLIST(VMObj, obj) objs;
	struct VMLIST(VMVar, var) vars;
	struct VMLIST(VMStream, stream) streams;
	const VM* fallback;                      // Read vars and funcs from here if not found in the current VM
	void* ext;
};

VM* NewVM(VM* fallback);
void DelVM(VM*);
bool ExecVM(VM*);
void ExitVM(VM*, bool retVal);

VMProg* NewVMProg(void);
void DelVMProg(VMProg*);

VMScope* VMPushScope(VM*, VMScopeFlags);
void VMPopScope(VM*);

VMObjID VMNewObj(VM*, VMObjType);
#define VMDelObj VMDecObjRef
void VMIncObjRef(VM*, VMObjID);
void VMDecObjRef(VM*, VMObjID);
bool VMCatObj(VM*, bool arrCat, VMObjID dest, VMObjID src);
bool VMCatStrToObj(VM*, bool arrCat, VMObjID, VMObjStrStorType, const char* str, size_t len);
void VMViewObj(VM*, VMObjID, VMObj*);
VMObjType VMGetRealObjType(VM*, VMObjID);
bool VMObjTruthy(VM*, VMObjID);

bool VMNewVar(VM*, const char* name, uint32_t nameCrc, bool local);
void VMDelVar(VM*, const char* name, uint32_t nameCrc);
VMObjID VMGetVar(const VM*, const char* name, uint32_t nameCrc);
bool VMSetVar(VM*, const char* name, uint32_t nameCrc, VMObjID);

bool VMPush(VM*, VMObjID, bool copy);
VMObjID VMPop(VM*);

void VMRaise(VM*);

#endif
