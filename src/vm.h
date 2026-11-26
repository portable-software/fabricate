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
#define VMLIST_ADD(VLB__b, VLB__n, VLB__d, VLB__en, VLB__ed, ...) do {\
    VLB__EXP((VLB__b), (VLB__en), (VLB__ed), (VLB__b).data[(VLB__b).len++].VLB__n = (VLB__d), __VA_ARGS__);\
} while (0)
#define VMLIST_NEXTPTR(VLB__b, VLB__n, VLB__o, VLB__en, VLB__ed, ...) do {\
    VLB__EXP((VLB__b), (VLB__en), (VLB__ed), (VLB__o) = &(VLB__b).data[(VLB__b).len++].VLB__n, __VA_ARGS__);\
} while (0)

typedef struct VM VM;

typedef struct VMProg VMProg;

typedef size_t VMObjID;
typedef size_t VMLblID;
#define VMOBJ_INVAL ((VMObjID)-1)
#define VMLBL_INVAL ((VMLblID)-1)

typedef uint32_t VMScopeFlags;
enum { // TODO: Reorder for cache reasons
	VMSCOPE_PROG     = 1U << 0,  // Push new program (also implies PC)
	VMSCOPE_PC       = 1U << 1,  // Push new program counter
	VMSCOPE_FRAME    = 1U << 2,  // Start a new frame
	VMSCOPE_VARS     = 1U << 3,  // Create a new local var scope
	VMSCOPE_ARGS     = 1U << 4,  // Use previous frame as arguments
	VMSCOPE_EXC      = 1U << 5,  // Set exception handler
	VMSCOPE_RESUME   = 1U << 6,  // Jump on pop
	VMSCOPE_SETOUT   = 1U << 7,  // Change the output state
	VMSCOPE_OUTPUT   = 1U << 8,  // Create output obj and concat it on pop
	VMSCOPE_RETURN   = 1U << 9,  // Scope uses 'return' to output
	VMSCOPE_ARRCAT   = 1U << 10, // Array concat on output
	VMSCOPE_NORETURN = 1U << 11, // Cannot use 'return' (do not use with RETURN)
	VMSCOPE_COLLAPSE = 1U << 12, // Collapse output
	VMSCOPE_ENOUT  = VMSCOPE_SETOUT | VMSCOPE_OUTPUT, // Enable output (do not use with RETURN)
	VMSCOPE_DISOUT = VMSCOPE_SETOUT                   // Disable output
};
typedef struct {
	VMProg* prog;
	size_t pc;
	VMLblID excLblId;
	VMLblID resumeLabelId;
} VMScopeParams;

typedef struct {
	size_t nameOff;
	unsigned line;    // 0 for unknown
	unsigned col;     // 0 for end of line
	unsigned endLine; // 0 for none
	unsigned endCol;  // 0 for end of line
} VMSrcLoc;

typedef uint_fast8_t VMCallFlags;
enum {
	VMCALL_SILENT = 1U << 0, // Output will be discarded, use isRetTruthy instead
};
typedef bool (*VMCall)(VM*, VMCallFlags, size_t argc, VMObjID* argv, void* ctx, VMObjID* ret, int* isRetTruthy, CharBuf* err);

typedef enum {
	VMOP_EXIT,    // Exit VM
	VMOP_SCOPE,   // Push a new scope
	VMOP_END,     // Pop the current scope
	VMOP_JMP,     // Jump
	VMOP_JNZ,     // Jump if last command output was true
	VMOP_JZ,      // Jump if last command output was false
	VMOP_CAT,     // Concat the top elem on the stack to the elem under it
	VMOP_PUSH,    // Create a new object on the stack
	VMOP_STR,     // Push a string
	VMOP_VAR,     // Push a variable
	VMOP_ARGC,    // Push the argument count
	VMOP_ARGV,    // Push an argument
	VMOP_ARGS,    // Push all arguments as an array
	VMOP_ARR,     // Create an array from elements in the current frame
	VMOP_EXPAND,  // Expand an array
	VMOP_INDEX,   // Index the current object
	VMOP_LEN,     // Get the length of the current object
	VMOP_SETVAR,  // Set a variable
	VMOP_SETELEM, // Set an array element in a variable
	VMOP_CALL,    // Call an external function
	VMOP_RAISE,   // Raise an exception
	VMOP_SETSRC,  // Set source code info
} VMOpCode;
typedef struct {
	uint_fast16_t code : 8;
	uint_fast16_t arrCat : 1;   // For CAT
	uint_fast16_t append : 1;   // For SETVAR and SETELEM
	union {
		struct {
			VMScopeFlags flags;
			size_t params;
		} scope;
		struct {
			VMLblID labelId;
		} jmp, jnz, jz;
		struct {
			size_t dataOff; // String table offset
			size_t dataLen;
		} str;
		struct {
			size_t nameOff;
			uint32_t nameCrc;
		} var;
		struct {
			size_t index;
		} argv;
		struct {
			size_t nameOff;
			uint32_t nameCrc;
		} setVar, setElem;
		struct {
			VMCall ptr;
			void* ctx;
		} call;
		struct {
			size_t srcId;
		} setSrc;
	} opnds;
} VMOp;
typedef struct VLB(VMOp) VMOpBuf;
struct VMProg {
	const VMOp* inst;
	const VMLblID* lblTbl;
	const char* strTbl;
	const VMScopeParams* scopeParams;
	const VMSrcLoc* srcTbl;
};

typedef enum {
	VMOBJ_NULL,
	VMOBJ_STR,
	VMOBJ_ARR,
	VMOBJ_STREAM,
	VMOBJ_COW
} VMObjType;
typedef struct {
	VMObjID objId;
	uint_fast8_t free : 1;
} VMObjCowSlot;
typedef struct VMObj VMObj;
struct VMObj {
	VMObjType type;
	union {
		struct {
			CharBuf text;
			uint_fast8_t ro : 1; // Is read-only
		} str;
		struct {
			struct VLB(VMObjID) elems;
			VMObjID freeRet;
			size_t freeRetIndex;
		} arr;
		IOStream* stream;
		struct {
			VMObjID objId;
			size_t start;
			size_t end;
			VMObjID nextCowId;
			VMObjID prevCowId;
		} cow;
	} data;
	size_t refCount;
	VMObjID firstCowId;
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
	VMScopeFlags flags : 30;
	VMScopeFlags isLastOutTruthy : 1;
	VMScopeFlags wroteToOut : 1;
	const VMProg* prog;
	size_t pc;
	size_t frame;
	VMObjID out;
	struct VMLIST(VMVar, var) localVars;
	size_t argc;
	size_t argv;
	VMLblID excLblId;                    // Label of exception handler (VMLBL_INVAL for none)
	VMLblID resumeLabelId;               // Jump on pop (VMLBL_INVAL for none)
	size_t srcId;
} VMScope;

struct VM {
	struct VLB(VMScope) scopes;
	struct VLB(VMObjID) stack;
	struct VMLIST(VMObj, obj) objs;
	struct VMLIST(VMVar, var) vars;
	const VM* fallback;                      // Read vars and funcs from here if not found in the current VM
	void* ctx;
};

VM* NewVM(VM* fallback, void* ctx, const VMProg*);
void DelVM(VM*);
bool ExecVM(VM*, CharBuf* err);

VMProg* NewVMProg(void);
void DelVMProg(VMProg*);

bool VMPushScope(VM*, VMScopeFlags, const VMScopeParams*);
bool VMPopScope(VM*);

VMObjID VMNewObj(VM*, VMObjType, ...);
#define VMDelObj VMDecObjRef
void VMIncObjRef(VM*, VMObjID);
void VMDecObjRef(VM*, VMObjID);
bool VMCatObj(VM*, bool arrCat, VMObjID dest, VMObjID src);
bool VMCatStrToObj(VM*, bool arrCat, VMObjID, VMObjStrStorType, const char* str, size_t len);
VMObjID VMDupObj(VM*, VMObjID);
void VMViewObj(VM*, VMObjID, VMObj*);
VMObjType VMGetRealObjType(VM*, VMObjID);
bool VMIsObjTruthy(VM*, VMObjID);
bool VMCollapseObj(VM*, VMObjID);

bool VMNewVar(VM*, const char* name, uint32_t nameCrc, bool local);
void VMDelVar(VM*, const char* name, uint32_t nameCrc);
VMObjID VMGetVar(const VM*, const char* name, uint32_t nameCrc);
bool VMSetVar(VM*, const char* name, uint32_t nameCrc, VMObjID);

bool VMPush(VM*, VMObjID, bool copy);
VMObjID VMPop(VM*);

void VMRaise(VM*);

#endif
