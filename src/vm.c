#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "utils.h"
#include "value.h"
#include "core.h"

VM vm;
CallFrame* frame;

static void closeUpValue(ObjUpValue* upvalue);
static void closeUpValues(Value* last);
static bool callValue(Value callee, int argCount);
void push(Value value);
Value pop();

static void resetStack() { vm.stackTop = vm.stack; }

void initVM() {
  vm.state = INITIALIZING;
  
  resetStack();
  initTable(&vm.strings);
  initTable(&vm.global);
  vm.upvalues = NULL;
  vm.framesCount = 0;

  vm.tryCatchStackCount = 0;
  vm.loopStackCount = 0;

  vm.objects = NULL;
  vm.objectsAssemblyLineEnd = NULL;
  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  vm.bytesAllocated = 0;
  vm.GCThreshold = 1024 * 1024;

  initializeCore(&vm);

  vm.state = INITIALIZED;
}

// All objects created after beginAssemblyLine is called (and the obj argument) 
// are considered part of the assembly line. Hence, cannot be garbage collected.
// When endAssemblyLine is called, the objects privileges are taken away. 
void beginAssemblyLine(Obj* obj) {
  if (vm.objectsAssemblyLineEnd != NULL) return;
  vm.objectsAssemblyLineEnd = obj;
}

void endAssemblyLine() {
  vm.objectsAssemblyLineEnd = NULL;
}

void freeVM() {
  freeObjects();
  freeTable(&vm.strings);
  freeTable(&vm.global);
}

ObjString* stackTrace() {
  char buffer[1024];
  int length = 0;

  for (int idx = vm.framesCount - 1; idx >= 0; idx--) {
    CallFrame* frame = &vm.frames[idx];
    ObjFunction* function = IS_FRAME_MODULE(frame) ? 
      FRAME_AS_MODULE(frame)->function : 
      FRAME_AS_CLOSURE(frame)->function;
    size_t instruction = frame->ip - function->chunk.code - 1;

    length += sprintf(&buffer[length], "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      length += sprintf(&buffer[length], "script\n");
    } else {
      if (IS_FRAME_MODULE(frame)) {
        length += sprintf(&buffer[length], "file %s\n", function->name->chars);
      } else if (function->name == CONSTANT_STRING("lambda function")) {
        length += sprintf(&buffer[length], "lambda function\n");  
      } else {
        length += sprintf(&buffer[length], "%s()\n", function->name->chars);
      }
    }
  }

  buffer[length] = '\0';

  return copyString(buffer, length);
} 

static void runtimeError(ObjString* stack, const char* format, ...) {
  va_list args;
  if (stack == NULL) {
    stack = stackTrace(); 
  }

  // Print error message
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);
  // Print track stace
  fprintf(stderr, stack->chars);

  resetStack();
  // SOFTWARE_ERROR
  exit(70);
}

static void recoverableRuntimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  char buffer[128];
  int length = vsprintf(buffer, format, args);
  buffer[length] = '\0'; 
  va_end(args);
  ObjString* message = copyString(buffer, length);
  ObjString* stack = stackTrace();

  // Throw outside of any try-catch block 
  if (vm.tryCatchStackCount == 0) {
    runtimeError(stack, "Uncaught Exception.\n%s", message->chars);
  }
  // Pop try-catch block
  TryCatch *tryCatch = &vm.tryCatchStack[--vm.tryCatchStackCount]; 
  // Generate stack trace

  // Pop Loops placed in intermediary frames between the "try-catch block" frame and
  // the "throw" frame. If a "throw" is found in a deep nested function, this ensure all
  // Loops created until there are popped. 
  while (&vm.frames[vm.framesCount - 1] != tryCatch->frame) {
    while (
      vm.loopStack > 0 &&
      vm.loopStack[vm.loopStackCount - 1].frame == &vm.frames[vm.framesCount - 1]
    ) {
      vm.loopStackCount--;
    }

    vm.framesCount--;
  }

  // Pop Loops placed in the same frame the try-catch block is
  while (
    vm.loopStackCount > 0 &&
    vm.loopStack[vm.loopStackCount - 1].frame == tryCatch->frame &&
    vm.loopStack[vm.loopStackCount - 1].outIp > tryCatch->startIp &&
    vm.loopStack[vm.loopStackCount - 1].outIp < tryCatch->outIp
  ) {
    vm.loopStackCount--;
  }

  // Get back to the closest try-catch block frame and move ip to the start of the catch block statement. 
  vm.stackTop = tryCatch->frameStackTop;
  frame = tryCatch->frame;
  frame->ip = tryCatch->catchIp;

  // If the catch block is compiled to receive a param, it should expect the param as a local variable in the stack
  if (tryCatch->hasCatchParameter) {
    ObjInstance *errorInstance = newInstance(vm.errorClass);
    tableSet(&errorInstance->properties, CONSTANT_STRING("message"), OBJ_VAL(message));
    tableSet(&errorInstance->properties, CONSTANT_STRING("stack"), OBJ_VAL(stack));

    push(OBJ_VAL(errorInstance));
  }

  // This cover an extreme corner case where we throw an enclosed function in a nested scope.
  closeUpValues(tryCatch->frameStackTop - 1);
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());
  int length = a->length + b->length;

  char* buffer = ALLOCATE(char, length + 1);
  memcpy(buffer, a->chars, a->length);
  memcpy(buffer + a->length, b->chars, b->length);
  buffer[length] = '\0';

  push(OBJ_VAL(takeString(buffer, length)));
}

static bool callEntry(ObjClosure* closure) {
  CallFrame* frame = &vm.frames[vm.framesCount++];
  frame->type = FRAME_TYPE_CLOSURE;
  frame->as.closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - 1;

  initTable(&frame->namespace);
  tableAddAll(&vm.global, &frame->namespace);

  return true;
}

static bool call(ObjClosure* closure, uint8_t argCount) {
  if (vm.framesCount == FRAMES_MAX) {
    runtimeError(NULL, "Stack overflow.");
  }

  CallFrame* frame = &vm.frames[vm.framesCount++];
  frame->type = FRAME_TYPE_CLOSURE;
  frame->namespace = vm.frames[vm.framesCount - 2].namespace;
  frame->as.closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;

  return true;
}

static bool callModule(ObjModule* module) {
  if (vm.framesCount == FRAMES_MAX) {
    runtimeError(NULL, "Stack overflow.");
  }

  CallFrame* frame = &vm.frames[vm.framesCount++];
  frame->type = FRAME_TYPE_MODULE;
  frame->as.module = module;
  frame->ip = module->function->chunk.code;
  frame->slots = vm.stackTop - 1;

  initTable(&frame->namespace);
  tableAddAll(&vm.global, &frame->namespace);

  return true;
}

static bool callNativeFn(NativeFn function, int argCount, bool isMethod) {
  // If this is a method, then handlers expects to receive the callee as argument.
  // We properly handle that by leveraging the bool type and avoiding branching.
  // function returns false <=> function returns error 
  if (!function(argCount, vm.stackTop - argCount - isMethod)) {
    Value fnReturn = pop(); // Must be an ObjString containing error message.
    recoverableRuntimeError(AS_CSTRING(fnReturn));
    return false;  
  } 

  Value fnReturn = pop();
  vm.stackTop -= argCount + 1; // pop from the stack the callee? + function arguments 
  push(fnReturn);
  return true;
}

// You can call whatever function as long as the argCount >= arity.
// consequences are:
//
// 1. If the function is not variadic, all arguments after the function
// arity are ignored.
// 
// 2. If the function is variadic, the function expect to receive N >= arity
// arguments and this should handle that.  
static inline void* resolveOverloadedMethod(void** methods, int argCount) {
  int arity = argCount >= ARGS_ARITY_MAX ? ARGS_ARITY_15 : argCount;

  for (int idx = arity; idx >= 0; idx--) {
    if (methods[idx] != NULL) {
      return methods[idx]; 
    }
  }

  for (int idx = arity + 1; idx < ARGS_ARITY_MAX; idx++) {
    if (methods[idx] != NULL) {
      recoverableRuntimeError("Expected %d arguments but got %d.", idx, argCount);
      return NULL;
    }
  }

  // unreachable
  return NULL;
}

static bool callConstructor(ObjClass* klass, int argCount) {
  ObjInstance* instance = newInstance(klass);
  Value initializer;
  
  vm.stackTop[-(argCount + 1)] = OBJ_VAL(instance);

  // MUST restrict the class name to ALWAYS be the constructor
  // i.e, blocking it to be a property
  if (tableGet(&klass->methods, klass->name, &initializer)) {
    if (AS_OVERLOADED_METHOD(initializer)->type == NATIVE_METHOD) {
      ObjNativeFn* native = (ObjNativeFn *) resolveOverloadedMethod((void **) AS_OVERLOADED_METHOD(initializer)->as.nativeMethods, argCount);

      if (native == NULL) {
        return false;
      }

      return callNativeFn(native->function, argCount, true);
    }
    else {
      ObjClosure* function = (ObjClosure *) resolveOverloadedMethod((void **) AS_OVERLOADED_METHOD(initializer)->as.userMethods, argCount);
      
      if (!function) {
        return false;
      }

      return call(function, argCount);
    }
  } else if (argCount != 0) {
    recoverableRuntimeError("Expected 0 arguments but got %d.", argCount);
    return false;
  }

  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLASS:
        return callConstructor(AS_CLASS(callee), argCount);
      case OBJ_BOUND_OVERLOADED_METHOD: {
        ObjBoundOverloadedMethod* overloadedMethod = AS_BOUND_OVERLOADED_METHOD(callee);
        
        vm.stackTop[-(argCount + 1)] = overloadedMethod->base;

        if (overloadedMethod->overloadedMethod->type == NATIVE_METHOD) {
          ObjNativeFn* native = 
            (ObjNativeFn*) resolveOverloadedMethod(((void **) &overloadedMethod->overloadedMethod->as.nativeMethods), argCount); 

          if (native == NULL) {
            return false;
          }

          return callNativeFn(native->function, argCount, true);
        } else {
          ObjClosure* function = 
            (ObjClosure*) resolveOverloadedMethod(((void **) &overloadedMethod->overloadedMethod->as.userMethods), argCount);

          if (function == NULL) {
            return false;
          }

          return call(function, argCount);
        }
        break;
      }
      case OBJ_CLOSURE: {
        // You can call whatever function as long as the argCount >= arity.
        // consequences are:
        //
        // 1. If the function is not variadic, all arguments after the function
        // arity are ignored.
        // 
        // 2. If the function is variadic, the function expect to receive N >= arity
        // arguments and this should handle that.
        if (AS_CLOSURE(callee)->function->arity > argCount) {
          recoverableRuntimeError("Expected %d arguments but got %d.", AS_CLOSURE(callee)->function->arity,
                      argCount);
          return false;
        }      

        return call(AS_CLOSURE(callee), argCount);
      }
      default:
        break;
    }
  }
 
  recoverableRuntimeError("Can only call functions.");
  return false;
}

Value peek(int distance) { return vm.stackTop[-(1 + distance)]; }

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static ObjUpValue* captureUpvalue(Value* local) {
  ObjUpValue* prevUpvalue = NULL;
  ObjUpValue* upvalue = vm.upvalues;

  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpValue* createdUpvalue = newUpValue(local);

  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.upvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpValue(ObjUpValue* upvalue){
  upvalue->closed = *upvalue->location;
  upvalue->location = &upvalue->closed;
}

static void closeUpValues(Value* last) {
  while (vm.upvalues != NULL && vm.upvalues->location >= last) {
    closeUpValue(vm.upvalues);
    vm.upvalues = vm.upvalues->next;
  }
}

static void defineMethod(ObjString* name) {
  Value method = peek(0);
  ObjClass* klass = AS_CLASS(peek(1));
  Value value;

  // Overloading existing method
  //
  // We can only overload user methods, otherwise we override the method overload name.
  // This limitation leads to a really strange corner case   
  if (
    tableGet(&klass->methods, name, &value) && 
    IS_OVERLOADED_METHOD(value) && 
    AS_OVERLOADED_METHOD(value)->type == USER_METHOD
  ) {
    AS_OVERLOADED_METHOD(value)->as.userMethods[AS_CLOSURE(method)->function->arity] = AS_CLOSURE(method);
    pop();
    return;  
  }

  // Creating new method overload and Assign the function to its slot
  ObjOverloadedMethod* overloadedMethod = newOverloadedMethod(name);
  beginAssemblyLine((Obj *) name);
  overloadedMethod->as.userMethods[AS_CLOSURE(method)->function->arity] = AS_CLOSURE(method);

  tableSet(&klass->methods, name, OBJ_VAL(overloadedMethod));
  endAssemblyLine();
  pop();
}

// Return an object class property, if it is a method, the object is bound to the method 
static bool objectClassProperty(Value base, ObjString* name, Value* value) {
  Value property = NIL_VAL;

  if (IS_OBJ(base)) {
    Obj* obj = AS_OBJ(base); 
    tableGet(&obj->klass->methods, name, &property);
  } else if (IS_NUMBER(base)) {
    tableGet(&vm.numberClass->methods, name, &property);
  } else if (IS_BOOL(base)) {
    tableGet(&vm.boolClass->methods, name, &property); 
  } else if (IS_NIL(base)) {
    tableGet(&vm.nilClass->methods, name, &property);
  } else {
    printf("todo unreachable.");
    exit(1);
  }

  if (IS_NIL(property)) return false;

  if (IS_OVERLOADED_METHOD(property)) {
    *value = OBJ_VAL(newBoundOverloadedMethod(base, AS_OVERLOADED_METHOD(property)));
  } else {
    *value = property;
  } 

  return true;
}

// Bind a superclass method to an object, usually used with as super.method()
static bool classBoundMethod(Value base, ObjClass* klass, ObjString* name, Value* value) {
  Value property;

  if (tableGet(&klass->methods, name, &property) && IS_OVERLOADED_METHOD(property)) {
    *value = OBJ_VAL(newBoundOverloadedMethod(base, AS_OVERLOADED_METHOD(property)));
    return true;
  }

  return false;
}

static bool invokeMethod(Value base, ObjString* name, uint8_t argCount) {
  Value value;
  
  if (IS_INSTANCE(base) && tableGet(&AS_INSTANCE(base)->properties, name, &value)) {
    return callValue(value, argCount);
  } 
  
  if (!objectClassProperty(base, name, &value)) {
    recoverableRuntimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  return callValue(value, argCount);
}

static bool getArrayItem(ObjArray* arr, Value index, Value* value) {
  if (!IS_NUMBER(index)) {
    recoverableRuntimeError("Array index must be a number.");
    return false;
  } else if (AS_NUMBER(index) < 0 || AS_NUMBER(index) >= arr->list.count) {
    // todo: should it be a runtime error?
    *value = NIL_VAL;
    return true;
  }

  *value = arr->list.values[(int) AS_NUMBER(index)];
  return true; 
}

static bool setArrayItem(ObjArray* arr, Value index, Value value) {
  if (!IS_NUMBER(index)) {
    recoverableRuntimeError("Array index must be a number.");
    return false;
  } else if (AS_NUMBER(index) < 0 || AS_NUMBER(index) >= arr->list.count) {
    recoverableRuntimeError("Array index out of bounds.");
    return false;
  }

  arr->list.values[(int) AS_NUMBER(index)] = value;
  return true; 
} 

static InterpretResult run() {
  frame = &vm.frames[vm.framesCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (IS_FRAME_MODULE(frame) ? FRAME_AS_MODULE(frame)->function->chunk.constants.values[READ_BYTE()] : FRAME_AS_CLOSURE(frame)->function->chunk.constants.values[READ_BYTE()]) 
#define READ_SHORT() \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING() (AS_STRING(READ_CONSTANT()))
#define BINARY_OP(valueType, op)                                \
  do {                                                          \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {           \
      recoverableRuntimeError("Operands must be numbers.");     \
      continue;                                                 \
    }                                                           \
    double b = AS_NUMBER(pop());                                \
    double a = AS_NUMBER(pop());                                \
    push(valueType(a op b));                                    \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("        ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    
    if (IS_FRAME_MODULE(frame)) {
      disassembleInstruction(
        &FRAME_AS_MODULE(frame)->function->chunk,
        (int)(frame->ip - FRAME_AS_MODULE(frame)->function->chunk.code));
    } else {
      disassembleInstruction(
        &FRAME_AS_CLOSURE(frame)->function->chunk,
        (int)(frame->ip - FRAME_AS_CLOSURE(frame)->function->chunk.code));
    }
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_ARRAY: {
        uint8_t length = READ_BYTE();
        ObjArray* array = newArray();
  
        for (int idx = 0; idx < length; idx++) {
          writeValueArray(&array->list, peek(length - 1 - idx));
        }

        while (length > 0) {
          pop();
          length--;
        }

        push(OBJ_VAL(array));
        break;
      }
      case OP_DEFINE_GLOBAL: {
        tableSet(&frame->namespace, READ_STRING(), peek(0));
        pop();
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;

        if (!tableGet(&frame->namespace, name, &value)) {
          recoverableRuntimeError("Undefined variable '%s'", name->chars);
          continue;
        }

        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();

        if (tableSet(&frame->namespace, name, peek(0))) {
          tableDelete(&frame->namespace, name);
          recoverableRuntimeError("Undefined variable '%s'", name->chars);
          continue;
        }
        break;
      }
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        frame->slots[READ_BYTE()] = peek(0);
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*FRAME_AS_CLOSURE(frame)->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *FRAME_AS_CLOSURE(frame)->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_GET_PROPERTY: {
        Value base;
        ObjString* name = READ_STRING();
        Value value = NIL_VAL;

        // When performing assign operation, the base is kept in the stack for facilitating the update
        if (READ_BYTE() == true) {
          base = peek(0);
        } else {
          base = pop();
        }

        if (IS_INSTANCE(base) && tableGet(&AS_INSTANCE(base)->properties, name, &value)) {
          push(value);
          break;
        }

        objectClassProperty(base, name, &value);
        push(value);
        break;
      }
      case OP_INVOKE: {
        ObjString* name = READ_STRING();
        uint8_t argCount = READ_BYTE();
        Value base = peek(argCount);

        if (!invokeMethod(base, name, argCount)) {
          continue;
        }

        frame = &vm.frames[vm.framesCount - 1];
        break;
      }
      case OP_SET_PROPERTY: {
        Value value = pop();
        Value base = pop();
        ObjString* name = READ_STRING();

        if (!IS_INSTANCE(base)) {
          recoverableRuntimeError("Cannot access property '%s'.", name->chars);
          continue;
        }

        tableSet(&AS_INSTANCE(base)->properties, name, value);
        push(value);
        break;
      }
      case OP_GET_ITEM: {
        Value identifier;
        Value base;

        if (READ_BYTE() == true) {
          identifier = peek(0);
          base = peek(1);
        } else {
          identifier = pop();
          base = pop();
        }

        Value value;
        if (IS_ARRAY(base)) {
          if (!getArrayItem(AS_ARRAY(base), identifier, &value)) {
            continue;
          }

          push(value);
        } else if (IS_OBJ(base)) {
          // todo
        } else {
          recoverableRuntimeError("Cannot access property.");
          continue;
        }
        break;
      }
      case OP_SET_ITEM: {
        Value value = pop();
        Value identifier = pop();
        Value base = pop();

        if (IS_ARRAY(base)) {
          if (!setArrayItem(AS_ARRAY(base), identifier, value)) {
            continue;
          }
          push(value);
        } else if (IS_OBJ(base)) {
          // todo
        } else {
          recoverableRuntimeError("Cannot access property");
          continue;
        }
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_NAMED_LOOP: {   
        Value iterator = peek(0);
        Value iterationIdx = peek(1);
        int nextIdx = AS_NUMBER(iterationIdx) + 1;

        if (!IS_ARRAY(iterator)) {
          recoverableRuntimeError("Expected for each iterator variable to be iterable.");
          continue;
        }

        // Get out of the loop 
        if (nextIdx >= AS_ARRAY(iterator)->list.count) {
          Loop* loop = &vm.loopStack[vm.loopStackCount - 1];


          frame->ip = loop->outIp;
          // For loops are usually terminated after an expression that is persisted on the stack.
          // Although we dont have any explict expression in this for each, in order to comply with
          // the LOOP_GUARD implementation, we will be popping the dummy value it adds. We will also
          // be adding this dummy value where needed.
          vm.stackTop = loop->frameStackTop + 1;
          break;
        }

        // update iteration idx
        vm.stackTop[-2] = NUMBER_VAL(nextIdx); 
        // update iteration name
        vm.stackTop[-3] = AS_ARRAY(iterator)->list.values[nextIdx]; 
        break;
      }
      case OP_LOOP_GUARD: {
        if (vm.loopStackCount + 1 == LOOP_STACK_MAX) {
          runtimeError(NULL, "Cant stack more than %d loops.", LOOP_STACK_MAX);
        }

        uint16_t startOffset = READ_SHORT();
        uint16_t outOffset = READ_SHORT();

        // push loop
        Loop* loop = &vm.loopStack[vm.loopStackCount++];

        loop->frame = frame;
        loop->frameStackTop = vm.stackTop;
        loop->startIp = frame->ip + startOffset;
        loop->outIp = frame->ip + outOffset; 
        break;
      }
      case OP_LOOP_BREAK: {
        Loop* loop = &vm.loopStack[vm.loopStackCount - 1];

        // Pop any existing try-catch block inside loop block 
        while (
          vm.tryCatchStackCount > 0 &&
          vm.tryCatchStack[vm.tryCatchStackCount - 1].frame == frame &&
          vm.tryCatchStack[vm.tryCatchStackCount - 1].outIp > loop->startIp &&
          vm.tryCatchStack[vm.tryCatchStackCount - 1].outIp < loop->outIp 
        ) {
          vm.tryCatchStackCount--;
        }

        frame->ip = loop->outIp;
        // For loops are usually terminated after an expression that is persisted on the stack.
        // Hence, at the end of the loop there is an OP_POP to get rid of it. In this exceptional 
        // situation we have to push a dummy value to the stack that will be popped.  
        vm.stackTop = loop->frameStackTop + 1;

        closeUpValues(loop->frameStackTop - 1);
        break;
      }
      case OP_LOOP_CONTINUE: {
        Loop* loop = &vm.loopStack[vm.loopStackCount - 1];

        // Pop any existing try-catch block inside loop block 
        while (
          vm.tryCatchStackCount > 0 &&
          vm.tryCatchStack[vm.tryCatchStackCount - 1].frame == frame &&
          vm.tryCatchStack[vm.tryCatchStackCount - 1].outIp > loop->startIp &&
          vm.tryCatchStack[vm.tryCatchStackCount - 1].outIp < loop->outIp 
        ) {
          vm.tryCatchStackCount--;
        }

        frame->ip = loop->startIp;
        vm.stackTop = loop->frameStackTop;

        closeUpValues(loop->frameStackTop - 1);
        break;
      }
      case OP_LOOP_GUARD_END: {
        // pop loop
        vm.loopStackCount--;
        break;
      }
      case OP_TRY_CATCH: {
        if (vm.tryCatchStackCount + 1 == TRY_CATCH_STACK_MAX) {
          runtimeError(NULL, "Cant stack more than %d try-catch blocks.", TRY_CATCH_STACK_MAX);
        }

        uint16_t catchOffset = READ_SHORT();
        uint16_t outOffset = READ_SHORT();
        bool hasCatchParameter = READ_BYTE();
        
        // push try-catch block
        TryCatch* tryCatch = &vm.tryCatchStack[vm.tryCatchStackCount++];

        tryCatch->frame = frame;
        tryCatch->frameStackTop = vm.stackTop;
        tryCatch->startIp = frame->ip;
        tryCatch->catchIp = frame->ip + catchOffset;
        tryCatch->outIp = frame->ip + outOffset;
        tryCatch->hasCatchParameter = hasCatchParameter;
        break;  
      }
      case OP_TRY_CATCH_TRY_END: {
        // Pop try-catch block
        TryCatch *tryCatch = &vm.tryCatchStack[--vm.tryCatchStackCount];

        /*
        * Move to the end of the try-catch block and skip catch statement
        * We are always gonna reach this intruction inside the same frame the try-catch block was created,
        * if we dont reach any throw statement. Hence, no need to update the current frame.
        */ 
        frame->ip = tryCatch->outIp;
        break;
      }
      case OP_THROW: {
        Value value = pop();

        // Throw outside of any try-catch block 
        if (vm.tryCatchStackCount == 0) {
          if (IS_INSTANCE(value) && AS_INSTANCE(value)->obj.klass == vm.errorClass) {
            ObjInstance * error = AS_INSTANCE(value);
            Value messageValue;
            Value stackValue;

            tableGet(&error->properties, CONSTANT_STRING("message"), &messageValue);
            tableGet(&error->properties, CONSTANT_STRING("stack"), &stackValue);
            
            runtimeError(AS_STRING(stackValue), "Uncaught Exception.\n%s", AS_CSTRING(messageValue));
          } else {
          }
            runtimeError(NULL, "Uncaught Exception.\n%s", toString(value)->chars);
        }
      
        // Pop try-catch block
        TryCatch *tryCatch = &vm.tryCatchStack[--vm.tryCatchStackCount]; 

        // Pop Loops placed in intermediary frames between the "try-catch block" frame and
        // the "throw" frame. If a "throw" is found in a deep nested function, this ensure all
        // Loops created until there are popped. 
        while (&vm.frames[vm.framesCount - 1] != tryCatch->frame) {
          while (
            vm.loopStack > 0 &&
            vm.loopStack[vm.loopStackCount - 1].frame == &vm.frames[vm.framesCount - 1]
          ) {
            vm.loopStackCount--;
          }

          vm.framesCount--;
        }

        // Pop Loops placed in the same frame the try-catch block is
        while (
          vm.loopStackCount > 0 &&
          vm.loopStack[vm.loopStackCount - 1].frame == tryCatch->frame &&
          vm.loopStack[vm.loopStackCount - 1].outIp > tryCatch->startIp &&
          vm.loopStack[vm.loopStackCount - 1].outIp < tryCatch->outIp
        ) {
          vm.loopStackCount--;
        }

        // Get back to the closest try-catch block frame and move ip to the start of the catch block statement. 
        vm.stackTop = tryCatch->frameStackTop;
        frame = tryCatch->frame;
        frame->ip = tryCatch->catchIp;

        // If the catch block is compiled to receive a param, it should expect the param as a local variable in the stack
        if (tryCatch->hasCatchParameter) {
          push(value);
        }

        // This cover an extreme corner case where we throw an enclosed function in a nested scope.
        closeUpValues(tryCatch->frameStackTop - 1);
        break;
      }
      case OP_TRUE:
        push(BOOL_VAL(true));
        break;
      case OP_FALSE:
        push(BOOL_VAL(false));
        break;
      case OP_NIL:
        push(NIL_VAL);
        break;
      case OP_GREATER: {
        BINARY_OP(BOOL_VAL, >);
        break;
      }
      case OP_LESS: {
        BINARY_OP(BOOL_VAL, <);
        break;
      }
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          BINARY_OP(NUMBER_VAL, +);
        } else {
          recoverableRuntimeError("Operands must be two numbers or two strings.");
          continue;
        }

        break;
      }
      case OP_SUBTRACT: {
        BINARY_OP(NUMBER_VAL, -);
        break;
      }
      case OP_MULTIPLY: {
        BINARY_OP(NUMBER_VAL, *);
        break;
      }
      case OP_DIVIDE: {
        BINARY_OP(NUMBER_VAL, /);
        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();

        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(0))) {
          recoverableRuntimeError("Operand must be a number.");
          continue;
        }

        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_POP: {
        pop();
        break;
      }
      case OP_CALL: {
        uint8_t argCount = READ_BYTE();
        if (!callValue(peek(argCount), argCount)) {
          continue;
        }
        frame = &vm.frames[vm.framesCount - 1];
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int idx = 0; idx < closure->upvalueCount; idx++) {
          uint8_t index = READ_BYTE();
          uint8_t isLocal = READ_BYTE();

          if (isLocal) closure->upvalues[idx] = captureUpvalue(frame->slots + index);
          else closure->upvalues[idx] = FRAME_AS_CLOSURE(frame)->upvalues[index];
        }
        break;
      }
      case OP_CLASS: {
        push(OBJ_VAL(newClass(READ_STRING())));
        break;
      }
      case OP_INHERIT: {
        ObjClass* klass = AS_CLASS(pop());
        Value superclass = peek(0);

        if (!IS_CLASS(superclass)) {
          recoverableRuntimeError("Superclass must be a class.");
          continue;
        }

        tableAddAllInherintance(&AS_CLASS(superclass)->methods, &klass->methods);
        break;
      }
      case OP_SUPER: {
        ObjClass* klass = AS_CLASS(pop()); 
        Value base = pop();
        ObjString* name = READ_STRING();
        Value value;

        if (!classBoundMethod(base, klass, name, &value)) {
          recoverableRuntimeError("Cannot access method '%s'.", name->chars);
          continue;
        }

        push(value);
        break;
      }
      case OP_METHOD: {
        defineMethod(READ_STRING());
        break;
      }
      case OP_OBJECT: {
        // push placeholder value where object instance is gonna be stored
        push(NIL_VAL);
        callConstructor(vm.klass, 0);  
        Value base = pop();
        int propertiesCount = READ_BYTE();

        while (propertiesCount > 0) {
          Value value = pop();
          ObjString* key = AS_STRING(pop());
          
          tableSet(&AS_INSTANCE(base)->properties, key, value);
          propertiesCount--;
        }

        push(base);

        break;        
      }
      case OP_CLOSE_UPVALUE: {
        closeUpValues(vm.stackTop - 1);
        pop();
        break;
      }
      case OP_EXPORT: {
        ObjString* name = READ_STRING();

        if (!tableSet(&FRAME_AS_MODULE(frame)->exports, name, pop())) {
          runtimeError(NULL, "Already exporting member with name '%s'.", name->chars);
        }

        break;
      }
      case OP_IMPORT: {
        ObjModule* module = AS_MODULE(READ_CONSTANT());

        if (!module->evaluated) {
          // Call module function
          push(OBJ_VAL(module->function));
          callModule(module);

          frame = &vm.frames[vm.framesCount - 1];
        } else {
          // If resolved, just copy it exports properties
          ObjInstance* exports = newInstance(vm.moduleExportsClass);
          tableAddAll(&module->exports, &exports->properties);
          push(OBJ_VAL(exports));
        }

        break;
      }
      case OP_RETURN: {
        Value result = pop();

        closeUpValues(frame->slots);

        if (IS_FRAME_MODULE(frame)) {
          // Free module variables namespace and flag as evaluated 
          freeTable(&frame->namespace);
          FRAME_AS_MODULE(frame)->evaluated = true;

          // Copy it exports properties to frame call result
          ObjInstance* exports = newInstance(vm.moduleExportsClass);
          tableAddAll(&FRAME_AS_MODULE(frame)->exports, &exports->properties);
          result = OBJ_VAL(exports);
        } 

        // Ensure loop blocks are popped if returned inside them
        while (vm.loopStackCount > 0 && vm.loopStack[vm.loopStackCount - 1].frame == frame) {
          vm.loopStackCount--;
        }

        // Ensure try-catch blocks are popped if returned inside them 
        while(vm.tryCatchStackCount > 0 && vm.tryCatchStack[vm.tryCatchStackCount - 1].frame == frame) {
          vm.tryCatchStackCount--;
        }

        vm.framesCount--;
        if (vm.framesCount == 0) {
          pop();
          return INTERPRET_OK;
        }

        vm.stackTop = frame->slots;
        push(result);

        frame = &vm.frames[vm.framesCount - 1];
        break;
      }

      default:
        break;
    }
  }

#undef BINARY_OP
#undef READ_CONSTANT
#undef READ_BYTE
}

InterpretResult interpret(const char* source, char* absPath) {
  ObjFunction* function = compile(source, absPath);

  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  vm.objectsAssemblyLineEnd = (Obj *) function;
  ObjClosure* closure = newClosure(function);
  vm.objectsAssemblyLineEnd = NULL;

  push(OBJ_VAL(closure));
  callEntry(closure);

  InterpretResult result = run();

  return result;
}
