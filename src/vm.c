#include "vm.h"

bool ExecVM(VM* vm) {
	FabErr err;

	while (!vm->exit) {
		VMScope* scope = &vm->scopes.data[vm->scopes.len - 1];
		VMProg* prog = scope->prog;
		VMOp* op = &prog->inst[scope->pc++];
		switch (op->code) {
			case VMOP_EXIT: {
				vm->exit = 1;
			} break;
			case VMOP_SCOPE: {
				if (!VMPushScope(vm, op->opnds.scope.flags)) goto fatal;
				++scope;
			} break;
			case VMOP_END: {
				VMPopScope(vm);
				--scope;
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
				if (obj == (VMObjID)-1) goto fatal;
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
			case VMOP_ARG: {
				if (op->opnds.arg.index != (size_t)-1) {
					if (!VMCatObj(
						vm, op->opnds.arg.arrCat, vm->stack.data[vm->stack.len - 1],
						(op->opnds.arg.index < scope->argc)
						? vm->stack.data[scope->argv + op->opnds.arg.index]
						: (VMObjID)-1
					)) goto fatal;
				} else {
					char str[21]; // a 64-bit unsigned decimal is 20 chars
					int len = sprintf(str, "%zu", scope->argc);
					if (len < 0) goto fatal;
					if (!VMCatStrToObj(
						vm, op->opnds.arg.arrCat, vm->stack.data[vm->stack.len - 1],
						VMOBJSTRSTOR_DUP, str, len
					)) goto fatal;
				}
			} break;
			case VMOP_ARR: {
				VMObjID arrId = VMNewObj(vm, VMOBJ_ARR);
				if (arrId == (VMObjID)-1) goto fatal;
				size_t frame = scope->frame;
				size_t frameLen = vm->stack.len - frame;
				VMObj* arr = &vm->objs.data[arrId].obj;
				VLB_EXPANDTO(arr->data.arr, frameLen, 2, 1, VMDelObj(vm, arrId); goto fatal;);
				for (size_t i = 0; i < frameLen; ++i) {
					arr->data.arr.data[i] = vm->stack.data[frame + i];
				}
				vm->stack.len = frame;
				VMPopScope(vm);
			} break;
			case VMOP_EXPAND: {
				size_t stackTop = vm->stack.len - 1;
				VMObjID arrId = vm->stack.data[stackTop];
				VMObjType type = VMGetRealObjType(vm, arrId);
				if (type != VMOBJ_ARR) break;
				VMObj arr;
				VMViewObj(vm, arrId, &arr);
				if (!arr.data.arr.len) break;
				VLB_EXPANDBY(vm->stack, arr.data.arr.len - 1, 2, 1, goto fatal;);
				for (size_t i = 0; i < arr.data.arr.len; ++i) {
					VMObjID obj = arr.data.arr.data[i];
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
				if (scope->out == (VMObjID)-1) flags |= VMCALL_SILENT;
				VMObjID ret = -1;
				int isRetTruthy = -1;
				err = op->opnds.call.ptr(vm, flags, frameLen, vm->stack.data + frame, op->opnds.call.ctx, &ret, &isRetTruthy);
				if (err) goto error;
				if (isRetTruthy == -1) isRetTruthy = VMObjTruthy(vm, ret);
				if (scope->out != (VMObjID)-1) VMCatObj(vm, true, scope->out, ret);
			} break;
			case VMOP_SETEXC: {
				scope->excLblId = op->opnds.setExc.labelId;
			} break;
			case VMOP_RAISE: {
				VMRaise(vm);
			} break;
			case VMOP_SETSRC: {
				scope->src = op->opnds.setSrc.src;
			} break;
		}
	}
	return true;

	error:
	// TODO: Print source info
	PUTERR(err);
	return false;

	fatal:
	PUTERR_LIT(INT);
	return false;
}
