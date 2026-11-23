#include "vm.h"

#include <stdarg.h>

static inline void FreeObj(VMObj*);
static inline void UnlinkCoWObj(VM*, VMObj*);

bool ExecVM(VM* vm, CharBuf* err) {
	while (1) {
		VMScope* scope = &vm->scopes.data[vm->scopes.len - 1];
		const VMProg* prog = scope->prog;
		const VMOp* op = &prog->inst[scope->pc++];
		switch (op->code) {
			case VMOP_EXIT: {
				--scope->pc;
				return true;
			} break;
			case VMOP_SCOPE: {
				if (!VMPushScope(
					vm, op->opnds.scope.flags,
					(op->opnds.scope.params != (size_t)-1) ? &prog->scopeParams[op->opnds.scope.params] : NULL
				)) goto fatal;
			} break;
			case VMOP_END: {
				if (!VMPopScope(vm)) goto fatal;
			} break;
			case VMOP_JMP: {
				scope->pc = prog->lblTbl[op->opnds.jmp.labelId];
			} break;
			case VMOP_JNZ: {
				if (scope->isLastOutTruthy) scope->pc = prog->lblTbl[op->opnds.jnz.labelId];
			} break;
			case VMOP_JZ: {
				if (!scope->isLastOutTruthy) scope->pc = prog->lblTbl[op->opnds.jz.labelId];
			} break;
			case VMOP_PUSH: {
				VMObjID obj = VMNewObj(vm, VMOBJ_NULL);
				if (obj == VMOBJ_INVAL) goto fatal;
				VLB_ADD(vm->stack, obj, 2, 1, VMDelObj(vm, obj); goto fatal;);
			} break;
			case VMOP_STR: {
				if (!VMCatStrToObj(
					vm, op->opnds.str.arrCat, vm->stack.data[vm->stack.len - 1],
					VMOBJSTRSTOR_DUP, prog->strTbl + op->opnds.str.dataOff, op->opnds.str.dataLen
				)) goto fatal;
			} break;
			case VMOP_VAR: {
				if (!VMCatObj(
					vm, op->opnds.var.arrCat, vm->stack.data[vm->stack.len - 1],
					VMGetVar(vm, prog->strTbl + op->opnds.var.nameOff, op->opnds.var.nameCrc)
				)) goto fatal;
			} break;
			case VMOP_ARGC: {
				char str[21]; // a 64-bit unsigned decimal is 20 chars
				int len = sprintf(str, "%zu", scope->argc);
				if (len < 0) goto fatal;
				if (!VMCatStrToObj(
					vm, op->opnds.argc.arrCat, vm->stack.data[vm->stack.len - 1],
					VMOBJSTRSTOR_DUP, str, len
				)) goto fatal;
			} break;
			case VMOP_ARGV: {
				size_t i = op->opnds.argv.index;
				if (!VMCatObj(
					vm, op->opnds.argv.arrCat, vm->stack.data[vm->stack.len - 1],
					(i < scope->argc) ? vm->stack.data[scope->argv + i] : VMOBJ_INVAL
				)) goto fatal;
			} break;
			case VMOP_ARGS: {
				size_t argc = scope->argc;
				size_t argv = scope->argv;
				VMObjID arrId = VMNewObj(vm, VMOBJ_ARR, argc, 1);
				if (arrId == VMOBJ_INVAL) goto fatal;
				VMObj* arr = &vm->objs.data[arrId].obj;
				for (size_t i = 0; i < argc; ++i) {
					if (!VMCatObj(vm, false, arr->data.arr.elems.data[i], vm->stack.data[argv + i])) goto args_fatal;
				}
				if (!VMCatObj(vm, op->opnds.args.arrCat, vm->stack.data[vm->stack.len - 1], arrId)) goto args_fatal;
				VMDelObj(vm, arrId);
				break;
				args_fatal:
				VMDelObj(vm, arrId);
				goto fatal;
			} break;
			case VMOP_ARR: {
				size_t frame = scope->frame;
				size_t frameLen = vm->stack.len - frame;
				VMObjID arrId = VMNewObj(vm, VMOBJ_ARR, frameLen, 0);
				if (arrId == VMOBJ_INVAL) goto fatal;
				VMObj* arr = &vm->objs.data[arrId].obj;
				for (size_t i = 0; i < frameLen; ++i) {
					arr->data.arr.elems.data[i] = vm->stack.data[frame + i];
				}
				scope->flags &= ~VMSCOPE_FRAME;
				vm->stack.len = frame;
				if (!VMPopScope(vm)) goto arr_fatal;
				if (!VMCatObj(vm, op->opnds.args.arrCat, vm->stack.data[vm->stack.len - 1], arrId)) goto arr_fatal;
				VMDelObj(vm, arrId);
				break;
				arr_fatal:
				VMDelObj(vm, arrId);
				goto fatal;
			} break;
			case VMOP_EXPAND: {
				size_t stackTop = vm->stack.len - 1;
				VMObjID arrId = vm->stack.data[stackTop];
				VMObjType type = VMGetRealObjType(vm, arrId);
				if (type != VMOBJ_ARR) break;
				VMObj arr;
				VMViewObj(vm, arrId, &arr);
				if (!arr.data.arr.elems.len) break;
				VLB_EXPANDBY(vm->stack, arr.data.arr.elems.len - 1, 2, 1, goto fatal;);
				for (size_t i = 0; i < arr.data.arr.elems.len; ++i) {
					VMObjID obj = arr.data.arr.elems.data[i];
					++vm->objs.data[obj].obj.refCount;
					vm->stack.data[stackTop + i] = obj;
				}
				VMDelObj(vm, arrId);
			} break;
			case VMOP_SETVAR: {
				VMObjID obj = VMPop(vm);
				VMSetVar(vm, prog->strTbl + op->opnds.setVar.nameOff, op->opnds.setVar.nameCrc, obj);
				scope->isLastOutTruthy = VMObjTruthy(vm, obj);
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
				if (isRetTruthy == -1) isRetTruthy = VMObjTruthy(vm, ret);
				scope->isLastOutTruthy = isRetTruthy;
				if (scope->out != VMOBJ_INVAL) {
					if (!VMCatObj(vm, scope->wroteToOut, scope->out, ret)) goto fatal;
					scope->wroteToOut = 1;
				}
				if (!VMPopScope(vm)) goto fatal;
			} break;
			case VMOP_RAISE: {
				VMRaise(vm);
			} break;
			case VMOP_SETSRC: {
				scope->src = op->opnds.setSrc.src;
			} break;
		}
	}

	fatal:
	CBAddStr(err, ERR_INT);
	return false;

	error:
	// TODO: Append source info to err
	return false;
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
	VLB_INIT(vm->streams, 4, goto fatal6;);
	vm->streams.firstFree = -1;
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
	scope->src = (VMSrcLoc){0};

	return vm;

	fatal6:
	VLB_FREE(vm->vars);
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
	VLB_ZINIT(scope->localVars);
	scope->localVars.firstFree = -1;
	if (!(flags & VMSCOPE_ARGS)) {
		scope->argc = prevScope->argc;
		scope->argv = prevScope->argv;
	} else {
		scope->argc = vm->stack.len - prevScope->frame;
		scope->argv = prevScope->frame;
	}
	scope->excLblId = (!(flags & VMSCOPE_EXC)) ? prevScope->excLblId : params->excLblId;
	scope->resumeLabelId = (!(flags & VMSCOPE_RESUME)) ? VMLBL_INVAL : params->resumeLabelId;
	scope->src = prevScope->src;

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
	if (flags & VMSCOPE_VARS) {
		tmp = scope->localVars.len;
		for (size_t i = 0; i < tmp; ++i) {
			VMVar* var = &scope->localVars.data[i].var;
			if (!var->name) continue;
			VMDelObj(vm, var->objId);
			free(var->name);
		}
	}
	if (flags & VMSCOPE_FRAME) {
		tmp = vm->stack.len;
		for (size_t i = scope->frame; i < tmp; ++i) {
			VMDelObj(vm, vm->stack.data[i]);
		}
		vm->stack.len = scope->frame;
	}
	if ((flags & VMSCOPE_ENOUT) == VMSCOPE_ENOUT) {
		if (!VMCatObj(vm, !!(scope->flags & VMSCOPE_ARRCAT), vm->stack.data[vm->stack.len - 1], scope->out)) return false;
		scope->wroteToOut = 1;
	}
	if (flags & VMSCOPE_RESUME) {
		prevScope->pc = prevScope->prog->lblTbl[scope->resumeLabelId];
	}

	--vm->scopes.len;

	return true;
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
		VMLIST_NEXTPTR(vm->objs, obj, obj, 2, 1, goto fatal;);
	}
	va_list va;
	obj->type = type;
	obj->refCount = 1;
	VLB_ZINIT(obj->cow);
	obj->cow.firstFree = -1;
	if (type == VMOBJ_NULL) return objId;
	va_start(va, type);
	switch (type) {
		#ifndef NDEBUG /* silence a warning */
		case VMOBJ_NULL:
		#endif
		case VMOBJ_STR: {
			VMObjStrStorType strStor = va_arg(va, int);
			if (!(strStor < 0)) {
				switch (strStor) {
					case VMOBJSTRSTOR_STATIC: {
						obj->data.str.text.data = va_arg(va, char*);
						obj->data.str.text.len = va_arg(va, size_t);
						obj->data.str.ro = 1;
					} break;
					case VMOBJSTRSTOR_XFER: {
						obj->data.str.text.data = va_arg(va, char*);
						obj->data.str.text.len = va_arg(va, size_t);
						obj->data.str.text.size = obj->data.str.text.len;
						obj->data.str.ro = 0;
					} break;
					case VMOBJSTRSTOR_DUP: {
						const char* str = va_arg(va, char*);
						size_t len = va_arg(va, size_t);
						VLB_ZINIT(obj->data.str.text);
						VLB_EXPANDTO(obj->data.str.text, len, 2, 1, goto fatal2;);
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
			VLB_EXPANDTO(obj->data.arr.elems, len, 2, 1, goto fatal2;);
			unsigned init = va_arg(va, unsigned);
			if (!init) break;
			size_t i;
			VMObjID elem;
			for (i = 0; i < len; ++i) {
				elem = VMNewObj(vm, VMOBJ_NULL);
				if (elem == VMOBJ_INVAL) goto arr_fatal;
				obj->data.arr.elems.data[i] = elem;
			}
			break;
			arr_fatal:
			VMDelObj(vm, elem);
			for (size_t j = 0; j < i; ++j) {
				VMDelObj(vm, obj->data.arr.elems.data[j]);
			}
			goto fatal2;
		} break;
		case VMOBJ_STREAM: {
			obj->data.stream = va_arg(va, VMStreamID);
		} break;
		case VMOBJ_COW: {
			VMObjID cowObjId = va_arg(va, VMObjID);
			VMObj* cowObj = &vm->objs.data[cowObjId].obj;
			size_t cowId;
			if (cowObj->cow.firstFree != (size_t)-1) {
				cowId = cowObj->cow.firstFree;
				cowObj->cow.data[cowId].slot = (VMObjCowSlot){.objId = objId};
				cowObj->cow.firstFree = cowObj->cow.data[cowId].nextFree;
			} else {
				cowId = cowObj->cow.len;
				VMLIST_ADD(cowObj->cow, slot, (VMObjCowSlot){.objId = objId}, 2, 1, goto fatal2;);
			}
			obj->data.cow.objId = cowObjId;
			obj->data.cow.off = va_arg(va, size_t);
			obj->data.cow.len = va_arg(va, size_t);
			obj->data.cow.cowId = cowId;
		} break;
	}
	va_end(va);
	return objId;

	fatal2:
	obj->refCount = 0;
	vm->objs.data[objId].nextFree = vm->objs.firstFree;
	vm->objs.firstFree = objId;
	fatal:
	return VMOBJ_INVAL;
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
		default: break;
	}
}
static inline void DelObj(VM* vm, VMObjID objId) {
	FreeObj(&vm->objs.data[objId].obj);
	vm->objs.data[objId].nextFree = vm->objs.firstFree;
	vm->objs.firstFree = objId;
}
void VMIncObjRef(VM* vm, VMObjID objId) {
	++vm->objs.data[objId].obj.refCount;
}
static inline void UnlinkCoWObj(VM* vm, VMObj* obj) {
	VMObj* cowObj = &vm->objs.data[obj->data.cow.objId].obj;
	size_t cowId = obj->data.cow.cowId;
	cowObj->cow.data[cowId].slot.free = 0;
	cowObj->cow.data[cowId].nextFree = cowObj->cow.firstFree;
	cowObj->cow.firstFree = cowId;
}
void VMDecObjRef(VM* vm, VMObjID objId) {
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
						VMDelObj(vm, elemId);
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
			} break;
			case VMOBJ_COW: {
				UnlinkCoWObj(vm, obj);
			} break;
			default: break;
		}
		DelObj(vm, objId);
	}
}
