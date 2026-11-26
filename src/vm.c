#include "vm.h"

#include <stdarg.h>

typedef struct {
	uint_fast8_t isRange : 1;
	uint_fast8_t hasStart : 1;
	uint_fast8_t hasEnd : 1;
	uint_fast8_t isStartNeg : 1;
	uint_fast8_t isEndNeg : 1;
	size_t start;
	size_t end;
} IdxOut;

static inline void FreeObj(VMObj*);
static inline void InitCoWObj(VM*, VMObjID, VMObjID target, size_t start, size_t end);
static inline bool UnCoWObj(VM*, VMObj*);
static inline void UnlinkCoWObj(VM*, VMObj*);
static inline bool InvalObjCoWRefs(VM*, VMObj*);
static inline bool InvalObjCoWRefsWRange(VM*, VMObj*, size_t start, size_t end);
static inline bool ParseIdx(const char*, IdxOut*);

bool ExecVM(VM* vm, CharBuf* err) {
	while (1) {
		VMScope* scope = &vm->scopes.data[vm->scopes.len - 1];
		const VMProg* prog = scope->prog;
		const VMOp* op = &prog->inst[scope->pc++];
		VMObjID objId;
		char szStr[21]; // A 64-bit unsigned decimal is 20 chars
		size_t size;
		switch ((VMOpCode)op->code) {
			case VMOP_EXIT: {
				return true;
			}
			case VMOP_SCOPE: {
				if (VMPushScope(
					vm, op->opnds.scope.flags,
					(op->opnds.scope.params != (size_t)-1) ? &prog->scopeParams[op->opnds.scope.params] : NULL
				)) break;
				goto fatal;
			}
			case VMOP_END: {
				if (VMPopScope(vm)) break;
				goto fatal;
			}
			case VMOP_JMP: {
				scope->pc = prog->lblTbl[op->opnds.jmp.labelId];
			} break;
			case VMOP_JNZ: {
				if (scope->isLastOutTruthy) scope->pc = prog->lblTbl[op->opnds.jnz.labelId];
			} break;
			case VMOP_JZ: {
				if (!scope->isLastOutTruthy) scope->pc = prog->lblTbl[op->opnds.jz.labelId];
			} break;
			case VMOP_CAT: {
				VMObjID objId = vm->stack.data[--vm->stack.len];
				if (VMCatObj(
					vm, ((op->arrCat) ? VMCATOBJ_ARRCAT : 0) | VMCATOBJ_XFER,
					vm->stack.data[vm->stack.len - 2], objId
				)) break;
				goto fatal;
			}
			case VMOP_PUSH: {
				push_null:
				objId = VMNewObj(vm, VMOBJ_NULL);
				push:
				if (objId == VMOBJ_INVAL) goto fatal;
				push_noCheck:
				VLB_ADD(vm->stack, objId, 2, 1, goto fatal;);
			} break;
			case VMOP_STR: {
				objId = VMNewObj(vm, VMOBJ_STR, VMOBJSTRSTOR_DUP, prog->strTbl + op->opnds.str.dataOff, op->opnds.str.dataLen);
				goto push;
			}
			case VMOP_VAR: {
				objId = VMGetVar(vm, prog->strTbl + op->opnds.var.nameOff, op->opnds.var.nameCrc);
				if (objId != VMOBJ_INVAL) goto push_noCheck;
				goto push_null;
			}
			case VMOP_ARGC: {
				size = scope->argc;
				push_sz:
				int len = sprintf(szStr, "%zu", size);
				if (len < 0) goto fatal;
				objId = VMNewObj(vm, VMOBJ_STR, VMOBJSTRSTOR_DUP, szStr, (size_t)len);
				goto push;
			}
			case VMOP_ARGV: {
				size_t i = op->opnds.argv.index;
				if (i < scope->argc) {
					objId = vm->stack.data[scope->argv + i];
					goto push_noCheck;
				}
				goto push_null;
			}
			case VMOP_ARGS: {
				size_t argc = scope->argc;
				size_t argv = scope->argv;
				objId = VMNewObj(vm, VMOBJ_ARR, argc, 1);
				if (objId == VMOBJ_INVAL) goto fatal;
				VMObj* arr = &vm->objs.data[objId].obj;
				for (size_t i = 0; i < argc; ++i) {
					if (!VMCatObj(vm, 0, arr->data.arr.elems.data[i], vm->stack.data[argv + i])) goto fatal;
				}
				goto push_noCheck;
			}
			case VMOP_ARR: {
				size_t frame = scope->frame;
				size_t frameLen = vm->stack.len - frame;
				objId = VMNewObj(vm, VMOBJ_ARR, frameLen, 0);
				if (objId == VMOBJ_INVAL) goto fatal;
				VMObj* arr = &vm->objs.data[objId].obj;
				for (size_t i = 0; i < frameLen; ++i) {
					arr->data.arr.elems.data[i] = vm->stack.data[frame + i];
				}
				scope->flags &= ~VMSCOPE_FRAME;
				vm->stack.len = frame;
				if (VMPopScope(vm)) goto push_noCheck;
				goto fatal;
			}
			case VMOP_EXPAND: {
				size_t stackTop = vm->stack.len - 1;
				objId = vm->stack.data[stackTop];
				VMObj arr;
				VMViewObj(vm, 0, objId, &arr);
				if (arr.type != VMOBJ_ARR) break;
				if (!arr.data.arr.elems.len) break;
				VLB_EXPANDBY(vm->stack, arr.data.arr.elems.len - 1, 2, 1, goto fatal;);
				for (size_t i = 0; i < arr.data.arr.elems.len; ++i) {
					VMObjID obj = arr.data.arr.elems.data[i];
					++vm->objs.data[obj].obj.refCount;
					vm->stack.data[stackTop + i] = obj;
				}
				if (VMDelObj(vm, objId)) break;
				goto fatal;
			}
			case VMOP_INDEX: {
				// TODO
				// REM: (size_t)-1 is out of range
			} break;
			case VMOP_LEN: {
				objId = vm->stack.data[--vm->stack.len];
				VMObj* obj = &vm->objs.data[objId].obj;
				VMObjType objType = VMGetRealObjType(vm, objId);
				switch (objType) {
					case VMOBJ_ARR:
						size = obj->data.arr.elems.len;
						break;
					case VMOBJ_STR:
						size = obj->data.str.text.len;
						break;
					default:
						if (VMDelObj(vm, objId)) goto push_null;
						goto fatal;
				}
				if (VMDelObj(vm, objId)) goto push_sz;
				goto fatal;
			}
			case VMOP_SETVAR: {
				objId = vm->stack.data[--vm->stack.len];
				if (!op->append) {
					setVar_set:
					if (!VMSetVar(vm, VMSETVAR_XFER, prog->strTbl + op->opnds.setVar.nameOff, op->opnds.setVar.nameCrc, objId)) goto fatal;
				} else {
					VMObjID varObjId = VMGetVar(vm, prog->strTbl + op->opnds.setVar.nameOff, op->opnds.setVar.nameCrc);
					if (varObjId == VMOBJ_INVAL) goto setVar_set;
					if (!VMCatObj(vm, VMCATOBJ_XFER, varObjId, objId)) goto fatal;
				}
				scope->isLastOutTruthy = VMIsObjTruthy(vm, objId);
			} break;
			case VMOP_SETELEM: {
				// TODO
			} break;
			case VMOP_CALL: {
				size_t frame = scope->frame;
				size_t frameLen = vm->stack.len - frame;
				VMCallFlags flags = 0;
				if (scope->out == VMOBJ_INVAL) flags |= VMCALL_SILENT;
				VMObjID ret = VMOBJ_INVAL;
				int isRetTruthy = -1;
				if (op->opnds.call.ptr(
					vm, flags,
					frameLen, vm->stack.data + frame,
					op->opnds.call.ctx,
					&ret, &isRetTruthy,
					err
				)) goto error;
				if (isRetTruthy == -1) isRetTruthy = VMIsObjTruthy(vm, ret);
				scope->isLastOutTruthy = isRetTruthy;
				if (scope->out != VMOBJ_INVAL) {
					if (!VMCatObj(vm, (scope->wroteToOut) ? VMCATOBJ_ARRCAT : 0, scope->out, ret)) goto fatal;
					scope->wroteToOut = 1;
				}
				if (!VMPopScope(vm)) goto fatal;
			} break;
			case VMOP_RAISE: {
				VMRaise(vm);
			} break;
			case VMOP_SETSRC: {
				scope->srcId = op->opnds.setSrc.srcId;
			} break;
		}
	}

	fatal:
	CBAddStr(err, ERR_INT);
	return false;

	error:
	// TODO: Prepend source info to err
	return false;
}

static inline bool ParseIdx(const char*, IdxOut*) {
	// TODO
}

VM* NewVM(VM* fallback, void* ctx, const VMProg* prog) {
	VM* vm = malloc(sizeof(*vm));
	if (!vm) goto fatal;

	VLB_INIT(vm->scopes, 8, goto fatal2;);
	VLB_INIT(vm->stack, 32, goto fatal3;);
	VLB_INIT(vm->objs, 32, goto fatal4;);
	vm->objs.firstFree = -1;
	VLB_INIT(vm->vars, 16, goto fatal5;);
	vm->vars.firstFree = -1;
	vm->fallback = fallback;
	vm->ctx = ctx;

	vm->scopes.len = 1;
	VMScope* scope = &vm->scopes.data[0];
	scope->flags = VMSCOPE_PROG | VMSCOPE_PC | VMSCOPE_VARS | VMSCOPE_NORETURN;
	scope->isLastOutTruthy = 0;
	scope->prog = prog;
	scope->pc = 0;
	scope->frame = 0;
	scope->out = VMOBJ_INVAL;
	VLB_ZINIT(scope->localVars);
	scope->localVars.firstFree = -1;
	scope->argc = 0;
	scope->argv = 0;
	scope->excLblId = VMLBL_INVAL;
	scope->srcId = -1;

	return vm;

	fatal5:
	VLB_FREE(vm->objs);
	fatal4:
	VLB_FREE(vm->stack);
	fatal3:
	VLB_FREE(vm->scopes);
	fatal2:
	free(vm);
	fatal:
	return NULL;
}
void DelVM(VM* vm) {
	(void)vm;
	// TODO
}

bool VMPushScope(VM* vm, VMScopeFlags flags, const VMScopeParams* params) {
	VMScope* prevScope = &vm->scopes.data[vm->scopes.len - 1];
	VMScope* scope;
	VLB_NEXTPTR(vm->scopes, scope, 2, 1, goto fatal;);

	if (!(flags & VMSCOPE_RETURN)) flags |= prevScope->flags & VMSCOPE_NORETURN;
	else flags |= VMSCOPE_SETOUT;
	if (!(flags & VMSCOPE_SETOUT)) flags |= prevScope->flags & VMSCOPE_OUTPUT;

	scope->flags = flags;
	scope->prog = (!(flags & VMSCOPE_PROG)) ? prevScope->prog : params->prog;
	scope->pc = (!(flags & (VMSCOPE_PROG | VMSCOPE_PC))) ? prevScope->pc : params->pc;
	scope->frame = (!(flags & VMSCOPE_FRAME)) ? prevScope->frame : vm->stack.len;
	if (!(flags & VMSCOPE_SETOUT)) {
		scope->isLastOutTruthy = prevScope->isLastOutTruthy;
		scope->out = prevScope->out;
	} else {
		scope->isLastOutTruthy = 0;
		if (!(flags & VMSCOPE_OUTPUT)) {
			scope->out = VMOBJ_INVAL;
		} else {
			VMObjID out = VMNewObj(vm, VMOBJ_NULL);
			if (out == VMOBJ_INVAL) goto fatal2;
			scope->out = out;
		}
	}
	if (!(flags & VMSCOPE_VARS)) {
		scope->localVars = prevScope->localVars;
	} else {
		VLB_ZINIT(scope->localVars);
		scope->localVars.firstFree = -1;
	}
	if (!(flags & VMSCOPE_ARGS)) {
		scope->argc = prevScope->argc;
		scope->argv = prevScope->argv;
	} else {
		scope->argc = vm->stack.len - prevScope->frame;
		scope->argv = prevScope->frame;
	}
	scope->excLblId = (!(flags & VMSCOPE_EXC)) ? prevScope->excLblId : params->excLblId;
	scope->resumeLabelId = (!(flags & VMSCOPE_RESUME)) ? VMLBL_INVAL : params->resumeLabelId;
	scope->srcId = prevScope->srcId;

	fatal2:
	--vm->scopes.len;
	fatal:
	return NULL;
}
bool VMPopScope(VM* vm) {
	VMScope* prevScope = &vm->scopes.data[vm->scopes.len - 2];
	VMScope* scope = &vm->scopes.data[vm->scopes.len - 1];
	VMScopeFlags flags = scope->flags;

	size_t tmp;
	if (!(flags & VMSCOPE_VARS)) {
		prevScope->localVars = scope->localVars;
	} else {
		tmp = scope->localVars.len;
		for (size_t i = 0; i < tmp; ++i) {
			VMVar* var = &scope->localVars.data[i].var;
			if (!var->name) continue;
			if (!VMDelObj(vm, var->objId)) return false;
			free(var->name);
		}
	}
	if (flags & VMSCOPE_FRAME) {
		tmp = vm->stack.len;
		for (size_t i = scope->frame; i < tmp; ++i) {
			if (!VMDelObj(vm, vm->stack.data[i])) return false;
		}
		vm->stack.len = scope->frame;
	}
	if ((flags & VMSCOPE_ENOUT) == VMSCOPE_ENOUT) {
		VMObjID outObjId = scope->out;
		if (scope->flags & VMSCOPE_COLLAPSE) {
			VMObjID collObjId = VMCollapseObj(vm, outObjId);
			if (collObjId == VMOBJ_INVAL || !VMDelObj(vm, outObjId)) return false;
			outObjId = collObjId;
		}
		if (!VMCatObj(
			vm, ((scope->flags & VMSCOPE_ARRCAT) ? VMCATOBJ_ARRCAT : 0) | VMCATOBJ_XFER,
			vm->stack.data[vm->stack.len - 1], outObjId
		)) return false;
	}
	if (flags & VMSCOPE_RESUME) {
		prevScope->pc = prevScope->prog->lblTbl[scope->resumeLabelId];
	}

	--vm->scopes.len;

	return true;
}

static inline size_t FindVar(const VMVarList* varList, const char* name, uint32_t nameCrc) {
	register size_t len = varList->len;
	for (size_t i = 0; i < len; ++i) {
		VMVar* var = &varList->data[i].var;
		if (!var->name) continue;
		if (var->nameCrc == nameCrc && !strcmp(var->name, name)) return i;
	}
	return -1;
}
static inline size_t FindVarAndList(VM* vm, const char* name, uint32_t nameCrc, VMVarList** varListOut) {
	VMVarList* varList = &vm->scopes.data[vm->scopes.len - 1].localVars;
	size_t varId = FindVar(varList, name, nameCrc);
	if (varId == (size_t)-1) {
		varList = &vm->vars;
		varId = FindVar(varList, name, nameCrc);
		if (varId == (size_t)-1) return -1;
	}
	*varListOut = varList;
	return varId;
}
static inline size_t FindVarAndList_Const(const VM* vm, const char* name, uint32_t nameCrc, const VMVarList** varListOut) {
	const VMVarList* varList = &vm->scopes.data[vm->scopes.len - 1].localVars;
	size_t varId = FindVar(varList, name, nameCrc);
	if (varId == (size_t)-1) {
		varList = &vm->vars;
		varId = FindVar(varList, name, nameCrc);
		if (varId == (size_t)-1) return -1;
	}
	*varListOut = varList;
	return varId;
}
bool VMSetVar(VM* vm, VMSetVarFlags flags, const char* name, uint32_t nameCrc, VMObjID objId) {
	VMVarList* varList;
	size_t varId = FindVarAndList(vm, name, nameCrc, &varList);
	VMVar* var;
	if (varId != (size_t)-1) {
		var = &varList->data[varId].var;
		if (!VMDelObj(vm, var->objId)) return false;
		finish:
		var->objId = objId;
		if (!(flags & VMSETVAR_XFER)) VMIncObjRef(vm, objId);
		return true;
	}
	varList = (!(flags & VMSETVAR_LOCAL)) ? &vm->vars : &vm->scopes.data[vm->scopes.len - 1].localVars;
	if (varList->firstFree != (size_t)-1) {
		varId = varList->firstFree;
		var = &varList->data[varId].var;
		varList->firstFree = varList->data[varId].nextFree;
	} else {
		varId = varList->len;
		VMLIST_NEXTPTR(*varList, var, var, 2, 1, return false;);
	}
	char* dupName = DupStr(name);
	if (!dupName) return false;
	var->name = dupName;
	var->nameCrc = nameCrc;
	goto finish;
}
bool VMDelVar(VM* vm, const char* name, uint32_t nameCrc) {
	VMVarList* varList;
	size_t varId = FindVarAndList(vm, name, nameCrc, &varList);
	if (varId == (size_t)-1) return true;
	VMVar* var = &varList->data[varId].var;
	if (!VMDelObj(vm, var->objId)) return false;
	free(var->name);
	var->name = NULL;
	varList->data[varId].nextFree = varList->firstFree;
	varList->firstFree = varId;
	return true;
}
VMObjID VMGetVar(const VM* vm, const char* name, uint32_t nameCrc) {
	const VMVarList* varList;
	size_t varId = FindVarAndList_Const(vm, name, nameCrc, &varList);
	if (varId == (size_t)-1) return VMOBJ_INVAL;
	return varList->data[varId].var.objId;
}

VMObjID VMNewObj(VM* vm, VMObjType type, ...) {
	VMObj* obj;
	VMObjID objId;
	if (vm->objs.firstFree != (size_t)-1) {
		objId = vm->objs.firstFree;
		obj = &vm->objs.data[objId].obj;
		vm->objs.firstFree = vm->objs.data[objId].nextFree;
	} else {
		objId = vm->objs.len;
		VMLIST_NEXTPTR(vm->objs, obj, obj, 2, 1, return VMOBJ_INVAL;);
	}
	va_list va;
	obj->type = type;
	obj->refCount = 1;
	obj->firstCowId = VMOBJ_INVAL;
	if (type == VMOBJ_NULL) return objId;
	va_start(va, type);
	switch (type) {
		#ifndef NDEBUG /* silence a warning */
		case VMOBJ_NULL:
		#endif
		case VMOBJ_STR: {
			int strStor = va_arg(va, int);
			if (!(strStor < 0)) {
				char* str = va_arg(va, char*);
				size_t len = va_arg(va, size_t);
				switch ((VMObjStrStorType)strStor) {
					case VMOBJSTRSTOR_STATIC: {
						obj->data.str.text.data = str;
						obj->data.str.text.len = len;
						obj->data.str.ro = 1;
					} break;
					case VMOBJSTRSTOR_XFER: {
						obj->data.str.text.data = str;
						obj->data.str.text.len = len;
						obj->data.str.text.size = obj->data.str.text.len;
						obj->data.str.ro = 0;
					} break;
					case VMOBJSTRSTOR_DUP: {
						VLB_ZINIT(obj->data.str.text);
						VLB_EXPANDTO(obj->data.str.text, len, 2, 1, return VMOBJ_INVAL;);
						memcpy(obj->data.str.text.data, str, len);
						obj->data.str.ro = 0;
					} break;
				}
			} else {
				VLB_ZINIT(obj->data.str.text);
				obj->data.str.ro = 0;
			}
		} break;
		case VMOBJ_ARR: {
			VLB_ZINIT(obj->data.arr.elems);
			size_t len = va_arg(va, size_t);
			if (!len) break;
			VLB_EXPANDTO(obj->data.arr.elems, len, 2, 1, return VMOBJ_INVAL;);
			unsigned init = va_arg(va, unsigned);
			if (!init) break;
			size_t i;
			VMObjID elem;
			for (i = 0; i < len; ++i) {
				elem = VMNewObj(vm, VMOBJ_NULL);
				if (elem == VMOBJ_INVAL) return VMOBJ_INVAL;
				obj->data.arr.elems.data[i] = elem;
			}
			break;
		} break;
		case VMOBJ_STREAM: {
			obj->data.stream = va_arg(va, IOStream*);
		} break;
		case VMOBJ_COW: {
			InitCoWObj(vm, objId, va_arg(va, VMObjID), va_arg(va, size_t), va_arg(va, size_t));
		} break;
	}
	va_end(va);
	return objId;
}
static inline void FreeObj(VMObj* obj) {
	switch (obj->type) {
		case VMOBJ_STR:
			if (!obj->data.str.ro) free(obj->data.str.text.data);
			break;
		case VMOBJ_ARR:
			VLB_FREE(obj->data.arr.elems);
			break;
		case VMOBJ_STREAM:
			//IODelStream(obj->data.stream);
			break;
		case VMOBJ_NULL:
		case VMOBJ_COW:
			break;
	}
}
static inline bool UnlinkObj(VM* vm, VMObjID objId) {
	VMObj* obj = &vm->objs.data[objId].obj;
	if (!InvalObjCoWRefs(vm, obj)) return false;
	FreeObj(obj);
	vm->objs.data[objId].nextFree = vm->objs.firstFree;
	vm->objs.firstFree = objId;
	return true;
}
void VMIncObjRef(VM* vm, VMObjID objId) {
	++vm->objs.data[objId].obj.refCount;
}
bool VMDecObjRef(VM* vm, VMObjID objId) {
	VMObj* obj = &vm->objs.data[objId].obj;
	if (!--obj->refCount) {
		switch (obj->type) {
			case VMOBJ_ARR: {
				size_t resume = 0;
				obj->data.arr.freeRet = VMOBJ_INVAL;
				again:
				size_t arrLen = obj->data.arr.elems.len;
				for (size_t i = resume; i < arrLen; ++i) {
					VMObjID elemId = obj->data.arr.elems.data[i];
					VMObj* elem = &vm->objs.data[elemId].obj;
					if (elem->type != VMOBJ_ARR) {
						if (!VMDelObj(vm, elemId)) return false;
					} else if (!--elem->refCount) {
						elem->data.arr.freeRet = objId;
						elem->data.arr.freeRetIndex = i;
						objId = elemId;
						obj = elem;
						goto again;
					}
				}
				if (obj->data.arr.freeRet == VMOBJ_INVAL) break;
				objId = obj->data.arr.freeRet;
				resume = obj->data.arr.freeRetIndex;
				obj = &vm->objs.data[objId].obj;
				goto again;
			}
			case VMOBJ_COW:
				UnlinkCoWObj(vm, obj);
				return false;
			case VMOBJ_NULL:
			case VMOBJ_STR:
			case VMOBJ_STREAM:
				break;
		}
		if (!UnlinkObj(vm, objId)) return false;
	}
	return true;
}

// REM: Do not CoW empty string or array
bool VMCatObj(VM*, VMCatObjFlags, VMObjID dest, VMObjID src) {
	// TODO
}
bool VMCatStrToObj(VM*, VMCatObjFlags, VMObjID, VMObjStrStorType, const char* str, size_t len) {
	// TODO
}

bool VMViewObj(VM* vm, VMViewObjFlags flags, VMObjID origObjId, VMObj* out) {
	VMObjID objId = origObjId;
	VMObj* obj = &vm->objs.data[objId].obj;
	size_t off = 0, len = (size_t)-1;
	again:
	switch (obj->type) {
		case VMOBJ_NULL:
			break;
		case VMOBJ_STR:
			if (flags & VMVIEWOBJ_CSTR) goto cstr;
			if (len > obj->data.str.text.len) len = obj->data.str.text.len;
			out->data.str.text.data = obj->data.str.text.data + off;
			out->data.str.text.len = len;
			break;
		case VMOBJ_ARR:
			if (len > obj->data.arr.elems.len) len = obj->data.arr.elems.len;
			out->data.arr.elems.data = obj->data.arr.elems.data + off;
			out->data.arr.elems.len = len;
			break;
		case VMOBJ_STREAM:
			out->data.stream = obj->data.stream;
			break;
		case VMOBJ_COW:
			len = obj->data.cow.end - off + 1;
			while (1) {
				off += obj->data.cow.start;
				objId = obj->data.cow.objId;
				obj = &vm->objs.data[objId].obj;
				if (obj->type != VMOBJ_COW) break;
			}
			goto again;
	}
	out->type = obj->type;
	return true;

	cstr:
	if (objId == origObjId) {
		cstr_term:
		if (!CBNullTerm(&obj->data.str.text)) return false;
		out->data.str.text.data = obj->data.str.text.data;
		out->data.str.text.len = obj->data.str.text.len;
		return true;
	}
	obj = &vm->objs.data[origObjId].obj;
	if (UnCoWObj(vm, obj)) goto cstr_term;
	return false;
}

VMObjType VMGetRealObjType(const VM* vm, VMObjID objId) {
	while (1) {
		VMObj* obj = &vm->objs.data[objId].obj;
		if (obj->type != VMOBJ_COW) return obj->type;
		objId = obj->data.cow.objId;
	}
}
bool VMIsObjTruthy(VM* vm, VMObjID objId) {
	VMObj obj;
	VMViewObj(vm, 0, objId, &obj);
	switch (obj.type) {
		default:           return false;
		case VMOBJ_STR:    return !!obj.data.str.text.len;
		case VMOBJ_ARR:    return !!obj.data.arr.elems.len;
		case VMOBJ_STREAM: return true;
	}
}
bool VMCollapseObj(VM* vm, VMObjID objId) {
	VMObjType type = VMGetRealObjType(vm, objId);
	if (type != VMOBJ_ARR) return true;

	// TODO

	return false;
}

static inline void InitCoWObj(VM* vm, VMObjID objId, VMObjID tgtObjId, size_t start, size_t end) {
	VMObj* obj = &vm->objs.data[objId].obj;
	VMObj* tgtObj = &vm->objs.data[tgtObjId].obj;
	obj->data.cow.objId = tgtObjId;
	obj->data.cow.start = start;
	obj->data.cow.end = end;
	VMObjID nextObjId = tgtObj->firstCowId;
	obj->data.cow.nextCowId = nextObjId;
	obj->data.cow.prevCowId = VMOBJ_INVAL;
	tgtObj->firstCowId = objId;
	if (nextObjId == VMOBJ_INVAL) return;
	vm->objs.data[nextObjId].obj.data.cow.prevCowId = objId;
}
static inline bool CoWObjToObj(VM*, VMObj*) {
	// TODO
	return false;
}
static inline void UnlinkCoWObj(VM* vm, VMObj* obj) {
	VMObjID nextCowId = obj->data.cow.nextCowId;
	VMObjID prevCowId = obj->data.cow.prevCowId;
	if (prevCowId != VMOBJ_INVAL) {
		vm->objs.data[prevCowId].obj.data.cow.nextCowId = nextCowId;
	} else {
		vm->objs.data[obj->data.cow.objId].obj.firstCowId = nextCowId;
	}
	if (nextCowId != VMOBJ_INVAL) {
		vm->objs.data[nextCowId].obj.data.cow.prevCowId = prevCowId;
	}
}
static inline bool UnCoWObj(VM* vm, VMObj* obj) {
	UnlinkCoWObj(vm, obj);
	return CoWObjToObj(vm, obj);
}
static inline bool InvalObjCoWRefs(VM* vm, VMObj* obj) {
	VMObjID cowObjId = obj->firstCowId;
	obj->firstCowId = VMOBJ_INVAL;
	while (1) {
		if (cowObjId == VMOBJ_INVAL) return true;
		VMObj* cowObj = &vm->objs.data[cowObjId].obj;
		cowObjId = cowObj->data.cow.nextCowId;
		if (!CoWObjToObj(vm, cowObj)) return false;
	}
}
static inline bool InvalObjCoWRefsWRange(VM* vm, VMObj* obj, size_t start, size_t end) {
	VMObjID cowObjId = obj->firstCowId;
	while (1) {
		if (cowObjId == VMOBJ_INVAL) return true;
		VMObj* cowObj = &vm->objs.data[cowObjId].obj;
		cowObjId = cowObj->data.cow.nextCowId;
		size_t cowStart = cowObj->data.cow.start;
		size_t cowEnd = cowObj->data.cow.end;
		if (end >= cowStart && start <= cowEnd && !UnCoWObj(vm, cowObj)) return false;
	}
}

void VMRaise(VM*) {
	// TODO
}
