
#include "native.h"

#include "class.h"
#include "host.h"
#include "interp.h"
#include "obj.h"
#include "rodata.h"
#include <assert.h>
#include <stdarg.h>


SpkMethod *Spk_NewNativeMethod(SpkNativeCodeFlags flags, SpkNativeCode nativeCode) {
    SpkMethod *newMethod;
    size_t argumentCount, variadic;
    size_t size;
    SpkOpcode *ip;
    
    size = 0;
    if (flags & SpkNativeCode_LEAF) {
        size += 5; /* leaf, arg, native */
    } else {
        size += 13; /* save, ... rett */
    }
    size += 2; /* restore, ret */
    
    variadic = 0;
    switch (flags & SpkNativeCode_SIGNATURE_MASK) {
    case SpkNativeCode_ARGS_0: argumentCount = 0; break;
    case SpkNativeCode_ARGS_1: argumentCount = 1; break;
    case SpkNativeCode_ARGS_2: argumentCount = 2; break;
        
    case SpkNativeCode_ARGS_ARRAY:
        argumentCount = 0;
        variadic = 1;
        break;
        
    default: assert(0); /* XXX */
    }
    
    newMethod = SpkMethod_New(size);
    newMethod->nativeCode = nativeCode;
    
    ip = SpkMethod_OPCODES(newMethod);
    if (flags & SpkNativeCode_LEAF) {
        assert(!variadic && "SpkNativeCode_ARGS_ARRAY cannot be combined with SpkNativeCode_LEAF");
        *ip++ = Spk_OPCODE_LEAF;
        *ip++ = Spk_OPCODE_ARG;
        *ip++ = (SpkOpcode)argumentCount;
        *ip++ = (SpkOpcode)argumentCount;
        *ip++ = Spk_OPCODE_NATIVE;
    } else {
        size_t stackSize = 4;
        size_t contextSize =
            stackSize +
            argumentCount + variadic;
        *ip++ = Spk_OPCODE_SAVE;
        *ip++ = (SpkOpcode)contextSize;
        *ip++ = (SpkOpcode)stackSize;
        *ip++ = variadic ? Spk_OPCODE_ARG_VA : Spk_OPCODE_ARG;
        *ip++ = (SpkOpcode)argumentCount;
        *ip++ = (SpkOpcode)argumentCount;
        *ip++ = Spk_OPCODE_NATIVE;
        
        /* skip trampoline code */
        *ip++ = Spk_OPCODE_BRANCH_ALWAYS;
        *ip++ = 6;
        
        /* trampolines for re-entering interpreted code */
        *ip++ = Spk_OPCODE_SEND_MESSAGE_NS_VAR_VA;
        *ip++ = Spk_OPCODE_RET_TRAMP;
        *ip++ = Spk_OPCODE_SEND_MESSAGE_SUPER_NS_VAR_VA;
        *ip++ = Spk_OPCODE_RET_TRAMP;
    }
    
    *ip++ = Spk_OPCODE_RESTORE_SENDER;
    *ip++ = Spk_OPCODE_RET;
    
    return newMethod;
}


/*------------------------------------------------------------------------*/
/* routines to send messages from native code */

SpkUnknown *Spk_SendMessage(SpkInterpreter *interpreter,
                            SpkUnknown *obj,
                            unsigned int ns,
                            SpkUnknown *selector,
                            SpkUnknown *argumentArray)
{
    return SpkInterpreter_SendMessage(interpreter, obj, ns, selector, argumentArray);
}

static SpkUnknown *vSendMessage(SpkInterpreter *interpreter,
                                SpkUnknown *obj, unsigned int ns, SpkUnknown *selector, va_list ap)
{
    SpkUnknown *argumentList, *result;

    argumentList = SpkHost_ArgsFromVAList(ap);
    result = Spk_SendMessage(interpreter, obj, ns, selector, argumentList);
    Spk_DECREF(argumentList);
    return result;
}

static SpkUnknown *sendMessage(SpkInterpreter *interpreter,
                               SpkUnknown *obj, unsigned int ns, SpkUnknown *selector, ...)
{
    SpkUnknown *result;
    va_list ap;
    
    va_start(ap, selector);
    result = vSendMessage(interpreter, obj, ns, selector, ap);
    va_end(ap);
    return result;
}

SpkUnknown *Spk_Oper(SpkInterpreter *interpreter, SpkUnknown *obj, SpkOper oper, ...) {
    SpkUnknown *result;
    va_list ap;
    
    va_start(ap, oper);
    result = Spk_VOper(interpreter, obj, oper, ap);
    va_end(ap);
    return result;
}

SpkUnknown *Spk_VOper(SpkInterpreter *interpreter, SpkUnknown *obj, SpkOper oper, va_list ap) {
    return vSendMessage(interpreter, obj, Spk_METHOD_NAMESPACE_RVALUE, *Spk_operSelectors[oper].selector, ap);
}

SpkUnknown *Spk_Call(SpkInterpreter *interpreter, SpkUnknown *obj, SpkCallOper oper, ...) {
    SpkUnknown *result;
    va_list ap;
    
    va_start(ap, oper);
    result = Spk_VCall(interpreter, obj, oper, ap);
    va_end(ap);
    return result;
}

SpkUnknown *Spk_VCall(SpkInterpreter *interpreter, SpkUnknown *obj, SpkCallOper oper, va_list ap) {
    return vSendMessage(interpreter, obj, Spk_METHOD_NAMESPACE_RVALUE, *Spk_operCallSelectors[oper].selector, ap);
}

SpkUnknown *Spk_Attr(SpkInterpreter *interpreter, SpkUnknown *obj, SpkUnknown *name) {
    return Spk_SendMessage(interpreter, obj, Spk_METHOD_NAMESPACE_RVALUE, name, Spk_emptyArgs);
}

SpkUnknown *Spk_SetAttr(SpkInterpreter *interpreter, SpkUnknown *obj, SpkUnknown *name, SpkUnknown *value) {
    return sendMessage(interpreter, obj, Spk_METHOD_NAMESPACE_LVALUE, name, value, 0);
}

SpkUnknown *Spk_Send(SpkInterpreter *interpreter, SpkUnknown *obj, SpkUnknown *selector, ...) {
    SpkUnknown *result;
    va_list ap;
    
    va_start(ap, selector);
    result = Spk_VSend(interpreter, obj, selector, ap);
    va_end(ap);
    return result;
}

SpkUnknown *Spk_VSend(SpkInterpreter *interpreter, SpkUnknown *obj, SpkUnknown *selector, va_list ap) {
    return vSendMessage(interpreter, obj, Spk_METHOD_NAMESPACE_RVALUE, selector, ap);
}

SpkUnknown *Spk_SendWithArguments(SpkInterpreter *interpreter, SpkUnknown *obj, SpkUnknown *name,
                                  SpkUnknown *argumentArray)
{
    return Spk_SendMessage(interpreter,
                           obj,
                           Spk_METHOD_NAMESPACE_RVALUE,
                           name,
                           argumentArray);
}


/*------------------------------------------------------------------------*/
/* halting */

void Spk_Halt(int code, const char *message) {
    SpkHost_Halt(code, message);
}

void Spk_HaltWithFormat(int code, const char *format, ...) {
    va_list args;
    
    va_start(args, format);
    SpkHost_VHaltWithFormat(code, format, args);
    va_end(args);
}

void Spk_HaltWithString(int code, SpkUnknown *message) {
    SpkHost_HaltWithString(code, message);
}


/*------------------------------------------------------------------------*/
/* argument processing */

int Spk_IsArgs(SpkUnknown *op) {
    return SpkHost_IsArgs(op);
}

size_t Spk_ArgsSize(SpkUnknown *args) {
    return SpkHost_ArgsSize(args);
}

SpkUnknown *Spk_GetArg(SpkUnknown *args, size_t index) {
    return SpkHost_GetArg(args, index);
}
