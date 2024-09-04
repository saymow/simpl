#include "vm.h"

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "core.h"
#include "debug.h"
#include "memory.h"
#include "utils.h"
#include "value.h"

VM vm;

static void closeUpValue(ObjUpValue* upvalue);
static void closeUpValues(Thread* program, Value* last);
static bool callValue(Thread* program, Value callee, uint8_t argCount);
void recoverableRuntimeError(Thread* program, const char* format, ...);
void push(Thread* program, Value value);
Value pop(Thread* program);

static void resetStack(Thread* program) { program->stackTop = program->stack; }

void initProgram(Thread* program) {
  initTable(&program->global);
  resetStack(program);
  program->frame = NULL;
  program->upvalues = NULL;
  program->framesCount = 0;
  program->loopStackCount = 0;
  program->tryCatchStackCount = 0;
  program->switchStackCount = 0;
}

void freeProgram(Thread* program) { freeTable(&program->global); }

void initVM() {
  vm.state = INITIALIZING;

  initTable(&vm.strings);
  vm.threads = NULL;
  vm.locks = NULL;
  vm.semaphores = NULL;
  vm.threadsCount = 0;
  vm.objects = NULL;
  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  vm.bytesAllocated = 0;
  vm.GCThreshold = 1024 * 1024;
  vm.GCWhiteListCount = 0;

  pthread_mutexattr_init(&vm.memoryAllocationMutexAttr);
  pthread_mutexattr_settype(&vm.memoryAllocationMutexAttr,
                            PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&vm.memoryAllocationMutex, &vm.memoryAllocationMutexAttr);

  initProgram(&vm.program);
  initCore(&vm);

  vm.lambdaFunctionName = CONSTANT_STRING("lambda function");

  vm.state = INITIALIZED;
}

void freeVM() {
  freeProgram(&vm.program);
  freeObjects();
  freeTable(&vm.strings);
}

// GCWhiteList is not a thread-safe function.
// GCWhiteList should always be followed by a corresponding GCPopWhiteList call.
// We leverage this fact to guarantuee mutual exclusion.
Obj* GCWhiteList(Obj* obj) {
  // Lock memory allocation area
  pthread_mutex_lock(&vm.memoryAllocationMutex);
  vm.GCWhiteList[vm.GCWhiteListCount++] = obj;
  return obj;
}

void GCPopWhiteList() {
  vm.GCWhiteListCount--;
  // Unlock memory allocation area
  pthread_mutex_unlock(&vm.memoryAllocationMutex);
}

ObjString* stackTrace(Thread* program) {
  char buffer[1024];
  int length = 0;

  for (int idx = program->framesCount - 1; idx >= 0; idx--) {
    CallFrame* frame = &program->frames[idx];
    ObjFunction* function = IS_FRAME_MODULE(frame)
                                ? FRAME_AS_MODULE(frame)->function
                                : FRAME_AS_CLOSURE(frame)->function;
    size_t instruction = frame->ip - function->chunk.code - 1;

    length += sprintf(&buffer[length], "[line %d] in ",
                      function->chunk.lines[instruction]);
    if (function->name == NULL) {
      length += sprintf(&buffer[length], "script\n");
    } else {
      if (IS_FRAME_MODULE(frame)) {
        length += sprintf(&buffer[length], "file %s\n", function->name->chars);
      } else if (function->name == vm.lambdaFunctionName) {
        length += sprintf(&buffer[length], "lambda function\n");
      } else {
        length += sprintf(&buffer[length], "%s()\n", function->name->chars);
      }
    }
  }

  buffer[length] = '\0';

  return copyString(buffer, length);
}

static void runtimeError(Thread* program, ObjString* stack, const char* format,
                         ...) {
  va_list args;
  if (stack == NULL) {
    stack = stackTrace(program);
  }

  // Print error message
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);
  // Print track stace
  fprintf(stderr, stack->chars);

  resetStack(program);
  // SOFTWARE_ERROR
  exit(70);
}

void recoverableRuntimeError(Thread* program, const char* format, ...) {
  va_list args;
  va_start(args, format);
  char buffer[128];
  int length = vsprintf(buffer, format, args);
  buffer[length] = '\0';
  va_end(args);
  ObjString* message =
      (ObjString*)GCWhiteList((Obj*)copyString(buffer, length));
  ObjString* stack = (ObjString*)GCWhiteList((Obj*)stackTrace(program));

  // Throw outside of any try-catch block
  if (program->tryCatchStackCount == 0) {
    runtimeError(program, stack, "Uncaught Exception.\n%s", message->chars);
  }
  // Pop try-catch block
  TryCatch* tryCatch = &program->tryCatchStack[--program->tryCatchStackCount];
  // Generate stack trace

  // Pop Loops placed in intermediary frames between the "try-catch block" frame
  // and the "throw" frame. If a "throw" is found in a deep nested function,
  // this ensure all Loops created until there are popped.
  while (&program->frames[program->framesCount - 1] != tryCatch->frame) {
    while (program->loopStackCount > 0 &&
           program->loopStack[program->loopStackCount - 1].frame ==
               &program->frames[program->framesCount - 1]) {
      program->loopStackCount--;
    }

    program->framesCount--;
  }

  // Pop Loops placed in the same frame the try-catch block is
  while (program->loopStackCount > 0 &&
         program->loopStack[program->loopStackCount - 1].frame ==
             tryCatch->frame &&
         program->loopStack[program->loopStackCount - 1].outIp >
             tryCatch->startIp &&
         program->loopStack[program->loopStackCount - 1].outIp <
             tryCatch->outIp) {
    program->loopStackCount--;
  }

  // Get back to the closest try-catch block frame and move ip to the start of
  // the catch block statement.
  program->stackTop = tryCatch->frameStackTop;
  program->frame = tryCatch->frame;
  program->frame->ip = tryCatch->catchIp;

  // If the catch block is compiled to receive a param, it should expect the
  // param as a local variable in the stack
  if (tryCatch->hasCatchParameter) {
    ObjInstance* errorInstance =
        (ObjInstance*)GCWhiteList((Obj*)newInstance(vm.errorClass));
    tableSet(&errorInstance->properties,
             (ObjString*)GCWhiteList((Obj*)CONSTANT_STRING("message")),
             OBJ_VAL(message));
    tableSet(&errorInstance->properties,
             (ObjString*)GCWhiteList((Obj*)CONSTANT_STRING("stack")),
             OBJ_VAL(stack));
    // Pop "stack" ObjString
    GCPopWhiteList();
    // Pop "message" ObjString
    GCPopWhiteList();
    // Pop errorInstance ObjInstance
    GCPopWhiteList();

    push(program, OBJ_VAL(errorInstance));
  }

  // This cover an extreme corner case where we throw an enclosured function in
  // a nested scope.
  closeUpValues(program, tryCatch->frameStackTop - 1);
  // Pop stack ObjString
  GCPopWhiteList();
  // Pop message ObjString
  GCPopWhiteList();
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(Thread* program) {
  ObjString* b = AS_STRING(peek(program, 0));
  ObjString* a = AS_STRING(peek(program, 1));
  int length = a->length + b->length;

  char* buffer = ALLOCATE(char, length + 1);
  memcpy(buffer, a->chars, a->length);
  memcpy(buffer + a->length, b->chars, b->length);
  buffer[length] = '\0';

  Value value = OBJ_VAL(takeString(buffer, length));

  pop(program);
  pop(program);
  push(program, value);
}

bool callEntry(Thread* thread, ObjClosure* closure) {
  thread->frame = &thread->frames[thread->framesCount++];
  thread->frame->type = FRAME_TYPE_CLOSURE;
  thread->frame->as.closure = closure;
  thread->frame->ip = closure->function->chunk.code;
  thread->frame->slots = thread->stackTop - 1;

  initTable(&thread->frame->namespace);
  tableAddAll(&thread->global, &thread->frame->namespace);

  return true;
}

static bool call(Thread* program, ObjClosure* closure, uint8_t argCount) {
  if (program->framesCount == FRAMES_MAX) {
    runtimeError(program, NULL, "Stack overflow.");
  }

  CallFrame* frame = &program->frames[program->framesCount++];
  frame->type = FRAME_TYPE_CLOSURE;
  frame->namespace = program->frames[program->framesCount - 2].namespace;
  frame->as.closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = program->stackTop - argCount - 1;

  return true;
}

static bool callModule(Thread* program, ObjModule* module) {
  if (program->framesCount == FRAMES_MAX) {
    runtimeError(program, NULL, "Stack overflow.");
  }

  CallFrame* frame = &program->frames[program->framesCount++];
  frame->type = FRAME_TYPE_MODULE;
  frame->as.module = module;
  frame->ip = module->function->chunk.code;
  frame->slots = program->stackTop - 1;

  initTable(&frame->namespace);
  tableAddAll(&program->global, &frame->namespace);

  return true;
}

static bool callNativeFn(Thread* program, NativeFn function, int argCount,
                         bool isMethod) {
  // If this is a method, then handlers expects to receive the callee as
  // argument. We properly handle that by leveraging the bool type and avoiding
  // branching. function returns false <=> function returns error
  if (!function(program, argCount, program->stackTop - argCount - isMethod)) {
    Value fnReturn =
        pop(program);  // Must be an ObjString containing error message.
    recoverableRuntimeError(program, AS_CSTRING(fnReturn));
    return false;
  }

  Value fnReturn = pop(program);
  program->stackTop -=
      argCount + 1;  // pop from the stack the callee? + function arguments
  push(program, fnReturn);
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
static inline void* resolveOverloadedMethod(Thread* program, void** methods,
                                            int argCount) {
  int arity = argCount >= ARGS_ARITY_MAX ? ARGS_ARITY_15 : argCount;

  for (int idx = arity; idx >= 0; idx--) {
    if (methods[idx] != NULL) {
      return methods[idx];
    }
  }

  for (int idx = arity + 1; idx < ARGS_ARITY_MAX; idx++) {
    if (methods[idx] != NULL) {
      recoverableRuntimeError(program, "Expected %d arguments but got %d.", idx,
                              argCount);
      return NULL;
    }
  }

  // unreachable
  return NULL;
}

static bool callConstructor(Thread* program, ObjClass* klass, int argCount) {
  ObjInstance* instance = newInstance(klass);
  Value initializer;

  program->stackTop[-(argCount + 1)] = OBJ_VAL(instance);

  // MUST restrict the class name to ALWAYS be the constructor
  // i.e, blocking it to be a property
  if (tableGet(&klass->methods, klass->name, &initializer)) {
    if (AS_OVERLOADED_METHOD(initializer)->type == NATIVE_METHOD) {
      ObjNativeFn* native = (ObjNativeFn*)resolveOverloadedMethod(
          program, (void**)AS_OVERLOADED_METHOD(initializer)->as.nativeMethods,
          argCount);

      if (native == NULL) {
        return false;
      }

      return callNativeFn(program, native->function, argCount, true);
    } else {
      ObjClosure* function = (ObjClosure*)resolveOverloadedMethod(
          program, (void**)AS_OVERLOADED_METHOD(initializer)->as.userMethods,
          argCount);

      if (!function) {
        return false;
      }

      return call(program, function, argCount);
    }
  } else if (argCount != 0) {
    recoverableRuntimeError(program, "Expected 0 arguments but got %d.",
                            argCount);
    return false;
  }

  return true;
}

static bool callValue(Thread* program, Value callee, uint8_t argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLASS:
        return callConstructor(program, AS_CLASS(callee), argCount);
      case OBJ_BOUND_OVERLOADED_METHOD: {
        ObjBoundOverloadedMethod* overloadedMethod =
            AS_BOUND_OVERLOADED_METHOD(callee);

        program->stackTop[-(argCount + 1)] = overloadedMethod->base;

        if (overloadedMethod->overloadedMethod->type == NATIVE_METHOD) {
          ObjNativeFn* native = (ObjNativeFn*)resolveOverloadedMethod(
              program,
              ((void**)&overloadedMethod->overloadedMethod->as.nativeMethods),
              argCount);

          if (native == NULL) {
            return false;
          }

          return callNativeFn(program, native->function, argCount, true);
        } else {
          ObjClosure* function = (ObjClosure*)resolveOverloadedMethod(
              program,
              ((void**)&overloadedMethod->overloadedMethod->as.userMethods),
              argCount);

          if (function == NULL) {
            return false;
          }

          return call(program, function, argCount);
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
        // 2. If the function is variadic, the function expect to receive N >=
        // arity arguments and this should handle that.
        if (AS_CLOSURE(callee)->function->arity > argCount) {
          recoverableRuntimeError(program, "Expected %d arguments but got %d.",
                                  AS_CLOSURE(callee)->function->arity,
                                  argCount);
          return false;
        }

        return call(program, AS_CLOSURE(callee), argCount);
      }
      default:
        break;
    }
  }

  recoverableRuntimeError(program, "Can only call functions.");
  return false;
}

Value peek(Thread* program, int distance) {
  return program->stackTop[-(1 + distance)];
}

void push(Thread* program, Value value) {
  *program->stackTop = value;
  program->stackTop++;
}

Value pop(Thread* program) {
  program->stackTop--;
  return *program->stackTop;
}

static ObjUpValue* captureUpvalue(Thread* program, Value* local) {
  ObjUpValue* prevUpvalue = NULL;
  ObjUpValue* upvalue = program->upvalues;

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
    program->upvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpValue(ObjUpValue* upvalue) {
  upvalue->closed = *upvalue->location;
  upvalue->location = &upvalue->closed;
}

static void closeUpValues(Thread* program, Value* last) {
  while (program->upvalues != NULL && program->upvalues->location >= last) {
    closeUpValue(program->upvalues);
    program->upvalues = program->upvalues->next;
  }
}

static void defineMethod(Thread* program, ObjString* name) {
  Value method = peek(program, 0);
  ObjClass* klass = AS_CLASS(peek(program, 1));
  Value value;

  AS_CLOSURE(method)->function->name = name;

  // Overloading existing method
  //
  // We can only overload user methods, otherwise we override the method
  // overload name. This limitation leads to a really strange corner case
  if (tableGet(&klass->methods, name, &value) && IS_OVERLOADED_METHOD(value) &&
      AS_OVERLOADED_METHOD(value)->type == USER_METHOD) {
    AS_OVERLOADED_METHOD(value)
        ->as.userMethods[AS_CLOSURE(method)->function->arity] =
        AS_CLOSURE(method);
    pop(program);
    return;
  }

  // Creating new method overload and Assign the function to its slot
  ObjOverloadedMethod* overloadedMethod =
      (ObjOverloadedMethod*)GCWhiteList((Obj*)newOverloadedMethod(name));
  GCWhiteList((Obj*)name);
  overloadedMethod->as.userMethods[AS_CLOSURE(method)->function->arity] =
      AS_CLOSURE(method);

  tableSet(&klass->methods, name, OBJ_VAL(overloadedMethod));
  GCPopWhiteList();
  GCPopWhiteList();
  pop(program);
}

// Return an object class property, if it is a method, the object is bound to
// the method
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
    *value =
        OBJ_VAL(newBoundOverloadedMethod(base, AS_OVERLOADED_METHOD(property)));
  } else {
    *value = property;
  }

  return true;
}

// Bind a superclass method to an object, usually used with as super.method()
static bool classBoundMethod(Value base, ObjClass* klass, ObjString* name,
                             Value* value) {
  Value property;

  if (tableGet(&klass->methods, name, &property) &&
      IS_OVERLOADED_METHOD(property)) {
    *value =
        OBJ_VAL(newBoundOverloadedMethod(base, AS_OVERLOADED_METHOD(property)));
    return true;
  }

  return false;
}

static bool invokeMethod(Thread* program, Value base, ObjString* name,
                         uint8_t argCount) {
  Value value;

  if (IS_INSTANCE(base) &&
      tableGet(&AS_INSTANCE(base)->properties, name, &value)) {
    return callValue(program, value, argCount);
  }

  if (!objectClassProperty(base, name, &value)) {
    recoverableRuntimeError(program, "Undefined property '%s'.", name->chars);
    return false;
  }

  return callValue(program, value, argCount);
}

static inline void getArrayItem(Thread* program, ObjArray* arr, Value index,
                                Value* value) {
  if (!IS_NUMBER(index)) {
    recoverableRuntimeError(program, "Array index must be a number.");
    return;
  } else if (AS_NUMBER(index) < 0 || AS_NUMBER(index) >= arr->list.count) {
    // todo: should it be a runtime error?
    return;
  }

  *value = arr->list.values[(int)AS_NUMBER(index)];
  return;
}

static inline void getStringChar(Thread* program, ObjString* string,
                                 Value index, Value* value) {
  if (!IS_NUMBER(index)) {
    recoverableRuntimeError(program, "String index must be a number.");
    return;
  } else if (AS_NUMBER(index) < 0 || AS_NUMBER(index) >= string->length) {
    // todo: should it be a runtime error?
    return;
  }

  *value = OBJ_VAL(copyString(&string->chars[(int)AS_NUMBER(index)], 1));
}

static inline void setArrayItem(Thread* program, ObjArray* arr, Value index,
                                Value value) {
  if (!IS_NUMBER(index)) {
    recoverableRuntimeError(program, "Array index must be a number.");
    return;
  } else if (AS_NUMBER(index) < 0 || AS_NUMBER(index) >= arr->list.count) {
    recoverableRuntimeError(program, "Array index out of bounds.");
    return;
  }

  arr->list.values[(int)AS_NUMBER(index)] = value;
}

static inline void getInstanceProperty(Thread* program, ObjInstance* instance,
                                       Value index, Value* value) {
  if (!IS_STRING(index)) {
    recoverableRuntimeError(program, "Object property key must be a string.");
    return;
  }

  if (!tableGet(&instance->properties, AS_STRING(index), value)) {
    *value = NIL_VAL;
  }
}

static inline void setInstanceProperty(Thread* program, ObjInstance* instance,
                                       Value index, Value value) {
  if (!IS_STRING(index)) {
    recoverableRuntimeError(program, "Object property key must be a string.");
    return;
  }

  tableSet(&instance->properties, AS_STRING(index), value);
}

static inline void getObjectProperty(Thread* program, Obj* obj, Value index,
                                     Value* value) {
  if (!IS_STRING(index)) {
    recoverableRuntimeError(program, "Object property key must be a string.");
    return;
  }

  if (!tableGet(&obj->klass->methods, AS_STRING(index), value)) {
    *value = NIL_VAL;
  }
}

// String interpolation is handled in two pass:
//  - 1°) Compute resulting string length and store placeholder string values
//  - 2°) Fill resulting string, swapping placeholder literal for placeholder
//  string value
static inline Value stringInterpolation(Thread* program, ObjString* template) {
  ObjArray* valueArray = (ObjArray*)GCWhiteList((Obj*)newArray());
  int length = template->length;

  for (int i = 0; i < template->length; i++) {
    // template placeholder slot found
    if (template->chars[i] == '$' && i + 1 < template->length &&
        template->chars[i + 1] == '(') {
      int placeholderStart = i;

      // skip $(
      i += 2;

      // Since we are popping from one stack to other, the items order is
      // reversed. Which is exactly what we need.
      Value value = pop(program);
      ObjString* valueStr = (ObjString*)GCWhiteList((Obj*)toString(value));
      writeValueArray(&valueArray->list, OBJ_VAL(valueStr));
      GCPopWhiteList();

      // consume everything until placeholder end
      int isPlaceholderOpen = 1;
      while (isPlaceholderOpen > 0) {
        if (template->chars[i] == '(') isPlaceholderOpen++;
        if (template->chars[i] == ')') isPlaceholderOpen--;
        i++;
      }
      i--;

      // Although this is computed in a reverse way, it is able to correctly get
      // the resulting string length. It does so by incrementing the delta
      // (placeholder length - placeholder-value length).
      length = length - (i - placeholderStart + 1) + valueStr->length;
    }
  }

  char buffer[length + 1];

  int idx = 0;
  for (int i = 0; i < template->length; i++) {
    // template placeholder slot found
    if (template->chars[i] == '$' && i + 1 < template->length &&
        template->chars[i + 1] == '(') {
      // skip $(
      i += 2;

      // consume everything until placeholder end
      int isPlaceholderOpen = 1;
      while (isPlaceholderOpen > 0) {
        if (template->chars[i] == '(') isPlaceholderOpen++;
        if (template->chars[i] == ')') isPlaceholderOpen--;
        i++;
      }
      i--;

      // write placeholder value on the buffer
      ObjString* valueStr =
          AS_STRING(valueArray->list.values[--valueArray->list.count]);
      for (int j = 0; j < valueStr->length; j++) {
        buffer[idx++] = valueStr->chars[j];
      }
    } else {
      // write template-non-placeholder on the buffer
      buffer[idx++] = template->chars[i];
    }
  }

  GCPopWhiteList(&valueArray);
  buffer[idx] = '\0';

  return OBJ_VAL(copyString(buffer, idx));
}

InterpretResult run(Thread* program) {
  program->frame = &program->frames[program->framesCount - 1];

#define IS_WORKER_THREAD() &vm.program != program
#define READ_BYTE() (*program->frame->ip++)
#define READ_CONSTANT()                                      \
  (IS_FRAME_MODULE(program->frame)                           \
       ? FRAME_AS_MODULE(program->frame)                     \
             ->function->chunk.constants.values[READ_BYTE()] \
       : FRAME_AS_CLOSURE(program->frame)                    \
             ->function->chunk.constants.values[READ_BYTE()])
#define READ_SHORT()        \
  (program->frame->ip += 2, \
   (uint16_t)((program->frame->ip[-2] << 8) | program->frame->ip[-1]))
#define READ_STRING() (AS_STRING(READ_CONSTANT()))
#define BINARY_OP(program, valueType, op)                               \
  do {                                                                  \
    if (!IS_NUMBER(peek(program, 0)) || !IS_NUMBER(peek(program, 1))) { \
      recoverableRuntimeError(program, "Operands must be numbers.");    \
      continue;                                                         \
    }                                                                   \
    double b = AS_NUMBER(pop(program));                                 \
    double a = AS_NUMBER(pop(program));                                 \
    push(program, valueType(a op b));                                   \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("        ");
    for (Value* slot = program->stack; slot < program->stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");

    if (IS_FRAME_MODULE(program->frame)) {
      disassembleInstruction(
          &FRAME_AS_MODULE(program->frame)->function->chunk,
          (int)(program->frame->ip -
                FRAME_AS_MODULE(program->frame)->function->chunk.code));
    } else {
      disassembleInstruction(
          &FRAME_AS_CLOSURE(program->frame)->function->chunk,
          (int)(program->frame->ip -
                FRAME_AS_CLOSURE(program->frame)->function->chunk.code));
    }
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(program, constant);
        break;
      }
      case OP_STRING_INTERPOLATION: {
        ObjString* name = AS_STRING(READ_CONSTANT());
        push(program, stringInterpolation(program, name));
        break;
      }
      case OP_ARRAY: {
        uint8_t length = READ_BYTE();
        ObjArray* array = newArray();

        GCWhiteList((Obj*)array);
        for (int idx = 0; idx < length; idx++) {
          writeValueArray(&array->list, peek(program, length - 1 - idx));
        }
        GCPopWhiteList();

        while (length > 0) {
          pop(program);
          length--;
        }

        push(program, OBJ_VAL(array));
        break;
      }
      case OP_DEFINE_GLOBAL: {
        tableSet(&program->frame->namespace, READ_STRING(), peek(program, 0));
        pop(program);
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;

        if (!tableGet(&program->frame->namespace, name, &value)) {
          recoverableRuntimeError(program, "Undefined variable '%s'",
                                  name->chars);
          continue;
        }

        push(program, value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();

        if (tableSet(&program->frame->namespace, name, peek(program, 0))) {
          tableDelete(&program->frame->namespace, name);
          recoverableRuntimeError(program, "Undefined variable '%s'",
                                  name->chars);
          continue;
        }
        break;
      }
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(program, program->frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        program->frame->slots[READ_BYTE()] = peek(program, 0);
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(program,
             *FRAME_AS_CLOSURE(program->frame)->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *FRAME_AS_CLOSURE(program->frame)->upvalues[slot]->location =
            peek(program, 0);
        break;
      }
      case OP_GET_PROPERTY: {
        ObjString* name = READ_STRING();
        // When performing assign operation, the base is kept in the stack for
        // facilitating the update
        Value base = READ_BYTE() == true ? peek(program, 0) : pop(program);
        Value value = NIL_VAL;

        if (IS_INSTANCE(base) &&
            tableGet(&AS_INSTANCE(base)->properties, name, &value)) {
          push(program, value);
          break;
        }

        objectClassProperty(base, name, &value);
        push(program, value);
        break;
      }
      case OP_INVOKE: {
        ObjString* name = READ_STRING();
        uint8_t argCount = READ_BYTE();
        Value base = peek(program, argCount);

        if (!invokeMethod(program, base, name, argCount)) {
          continue;
        }

        program->frame = &program->frames[program->framesCount - 1];
        break;
      }
      case OP_SET_PROPERTY: {
        Value value = pop(program);
        Value base = pop(program);
        ObjString* name = READ_STRING();

        if (!IS_INSTANCE(base)) {
          recoverableRuntimeError(program, "Cannot access property '%s'.",
                                  name->chars);
          continue;
        }

        tableSet(&AS_INSTANCE(base)->properties, name, value);
        push(program, value);
        break;
      }
      case OP_GET_ITEM: {
        Value base;
        Value identifier;
        Value value = NIL_VAL;

        // When performing assign operation, the base and identifier is kept in
        // the stack for facilitating the update
        if (READ_BYTE() == true) {
          identifier = peek(program, 0);
          base = peek(program, 1);
        } else {
          identifier = pop(program);
          base = pop(program);
        }

        if (IS_ARRAY(base)) {
          getArrayItem(program, AS_ARRAY(base), identifier, &value);
        } else if (IS_STRING(base)) {
          getStringChar(program, AS_STRING(base), identifier, &value);
        } else if (IS_STRING(identifier)) {
          if (!(IS_INSTANCE(base) && tableGet(&AS_INSTANCE(base)->properties,
                                              AS_STRING(identifier), &value))) {
            objectClassProperty(base, AS_STRING(identifier), &value);
          }
        }

        push(program, value);
        break;
      }
      case OP_SET_ITEM: {
        Value value = pop(program);
        Value identifier = pop(program);
        Value base = pop(program);

        if (IS_ARRAY(base)) {
          setArrayItem(program, AS_ARRAY(base), identifier, value);
        } else if (IS_INSTANCE(base)) {
          setInstanceProperty(program, AS_INSTANCE(base), identifier, value);
        }

        push(program, value);
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        program->frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(program, 0))) program->frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        program->frame->ip -= offset;
        break;
      }
      case OP_RANGED_LOOP_SETUP: {
        Value startValue = peek(program, 2);
        Value endValue = peek(program, 1);
        Value stepValue = peek(program, 0);

        if (!IS_NUMBER(startValue)) {
          runtimeError(program, NULL,
                       "Expected range arguments to be numbers.");
        }

        double start = AS_NUMBER(startValue);
        double end;
        double step;

        if (IS_NIL(stepValue)) {
          if (IS_NIL(endValue)) {
            // handle "range(len)" -> "range(0, len, +-1)"
            end = start;
            start = 0;
            step = end > 0 ? 1 : -1;
          } else {
            // handle "range(start, end)" -> "range(start, end, 1)"
            if (!IS_NUMBER(endValue)) {
              runtimeError(program, NULL,
                           "Expected range arguments to be numbers.");
            }

            end = AS_NUMBER(endValue);
            step = start < end ? 1 : -1;
          }
        } else {
          // handle "for idx range(start, end, step)"
          if (!(IS_NUMBER(endValue) && IS_NUMBER(stepValue))) {
            runtimeError(program, NULL,
                         "Expected range arguments to be numbers.");
          }
          end = AS_NUMBER(endValue);
          step = AS_NUMBER(stepValue);
        }

        start -= step;

        program->stackTop[-3] = NUMBER_VAL(start);
        program->stackTop[-2] = NUMBER_VAL(end);
        program->stackTop[-1] = NUMBER_VAL(step);
        break;
      }
      case OP_RANGED_LOOP: {
        double current = AS_NUMBER(peek(program, 2));
        double end = AS_NUMBER(peek(program, 1));
        double step = AS_NUMBER(peek(program, 0));

        current += step;

        // Get out of the loop
        if (step > 0 ? current >= end : current <= end) {
          Loop* loop = &program->loopStack[program->loopStackCount - 1];

          program->frame->ip = loop->outIp;
          // For loops are usually terminated after an expression that is
          // persisted on the stack. Although we dont have any explict
          // expression in this for each, in order to comply with the LOOP_GUARD
          // implementation, we will be popping the dummy value it adds. We will
          // also be adding this dummy value where needed.
          program->stackTop = loop->frameStackTop + 1;
          break;
        }

        program->stackTop[-3] = NUMBER_VAL(current);
        break;
      }
      case OP_NAMED_LOOP: {
        Value iterator = peek(program, 0);
        Value iterationIdx = peek(program, 1);
        int nextIdx = AS_NUMBER(iterationIdx) + 1;

        if (!IS_ARRAY(iterator)) {
          recoverableRuntimeError(
              program, "Expected for each iterator variable to be iterable.");
          continue;
        }

        // Get out of the loop
        if (nextIdx >= AS_ARRAY(iterator)->list.count) {
          Loop* loop = &program->loopStack[program->loopStackCount - 1];

          program->frame->ip = loop->outIp;
          // For loops are usually terminated after an expression that is
          // persisted on the stack. Although we dont have any explict
          // expression in this for each, in order to comply with the LOOP_GUARD
          // implementation, we will be popping the dummy value it adds. We will
          // also be adding this dummy value where needed.
          program->stackTop = loop->frameStackTop + 1;
          break;
        }

        // update iteration idx
        program->stackTop[-2] = NUMBER_VAL(nextIdx);
        // update iteration name
        program->stackTop[-3] = AS_ARRAY(iterator)->list.values[nextIdx];
        break;
      }
      case OP_LOOP_GUARD: {
        if (program->loopStackCount + 1 == LOOP_STACK_MAX) {
          runtimeError(program, NULL, "Cant stack more than %d loops.",
                       LOOP_STACK_MAX);
        }

        uint16_t startOffset = READ_SHORT();
        uint16_t outOffset = READ_SHORT();

        // push loop
        Loop* loop = &program->loopStack[program->loopStackCount++];

        loop->frame = program->frame;
        loop->frameStackTop = program->stackTop;
        loop->startIp = program->frame->ip + startOffset;
        loop->outIp = program->frame->ip + outOffset;
        break;
      }
      case OP_LOOP_BREAK: {
        Loop* loop = &program->loopStack[program->loopStackCount - 1];

        // Pop any existing try-catch block inside loop block
        while (program->tryCatchStackCount > 0 &&
               program->tryCatchStack[program->tryCatchStackCount - 1].frame ==
                   program->frame &&
               program->tryCatchStack[program->tryCatchStackCount - 1].outIp >
                   loop->startIp &&
               program->tryCatchStack[program->tryCatchStackCount - 1].outIp <
                   loop->outIp) {
          program->tryCatchStackCount--;
        }

        program->frame->ip = loop->outIp;
        // For loops are usually terminated after an expression that is
        // persisted on the stack. Hence, at the end of the loop there is an
        // OP_POP to get rid of it. In this exceptional situation we have to
        // push a dummy value to the stack that will be popped.
        program->stackTop = loop->frameStackTop + 1;

        closeUpValues(program, loop->frameStackTop - 1);
        break;
      }
      case OP_LOOP_CONTINUE: {
        Loop* loop = &program->loopStack[program->loopStackCount - 1];

        // Pop any existing try-catch block inside loop block
        while (program->tryCatchStackCount > 0 &&
               program->tryCatchStack[program->tryCatchStackCount - 1].frame ==
                   program->frame &&
               program->tryCatchStack[program->tryCatchStackCount - 1].outIp >
                   loop->startIp &&
               program->tryCatchStack[program->tryCatchStackCount - 1].outIp <
                   loop->outIp) {
          program->tryCatchStackCount--;
        }

        program->frame->ip = loop->startIp;
        program->stackTop = loop->frameStackTop;

        closeUpValues(program, loop->frameStackTop - 1);
        break;
      }
      case OP_LOOP_GUARD_END: {
        // pop loop
        program->loopStackCount--;
        break;
      }
      case OP_TRY_CATCH: {
        if (program->tryCatchStackCount + 1 == TRY_CATCH_STACK_MAX) {
          runtimeError(program, NULL,
                       "Cant stack more than %d try-catch blocks.",
                       TRY_CATCH_STACK_MAX);
        }

        uint16_t catchOffset = READ_SHORT();
        uint16_t outOffset = READ_SHORT();
        bool hasCatchParameter = READ_BYTE();

        // push try-catch block
        TryCatch* tryCatch =
            &program->tryCatchStack[program->tryCatchStackCount++];

        tryCatch->frame = program->frame;
        tryCatch->frameStackTop = program->stackTop;
        tryCatch->startIp = program->frame->ip;
        tryCatch->catchIp = program->frame->ip + catchOffset;
        tryCatch->outIp = program->frame->ip + outOffset;
        tryCatch->hasCatchParameter = hasCatchParameter;
        break;
      }
      case OP_TRY_CATCH_TRY_END: {
        // Pop try-catch block
        TryCatch* tryCatch =
            &program->tryCatchStack[--program->tryCatchStackCount];

        // Move to the end of the try-catch block and skip catch statement
        // We are always gonna reach this intruction inside the same frame the
        // try-catch block was created, if we dont reach any throw statement.
        // Hence, no need to update the current frame.
        program->frame->ip = tryCatch->outIp;
        break;
      }
      case OP_THROW: {
        // Throw outside of any try-catch block
        if (program->tryCatchStackCount == 0) {
          Value value = pop(program);

          if (IS_INSTANCE(value) &&
              AS_INSTANCE(value)->obj.klass == vm.errorClass) {
            ObjInstance* error =
                (ObjInstance*)GCWhiteList((Obj*)AS_INSTANCE(value));
            Value messageValue;
            Value stackValue;

            tableGet(&error->properties,
                     (ObjString*)GCWhiteList((Obj*)CONSTANT_STRING("message")),
                     &messageValue);
            tableGet(&error->properties,
                     (ObjString*)GCWhiteList((Obj*)CONSTANT_STRING("stack")),
                     &stackValue);

            runtimeError(program, AS_STRING(stackValue),
                         "Uncaught Exception.\n%s", AS_CSTRING(messageValue));
          } else {
            runtimeError(program, NULL, "Uncaught Exception.\n%s",
                         toString(value)->chars);
          }
        }

        Value value = pop(program);
        // Pop try-catch block
        TryCatch* tryCatch =
            &program->tryCatchStack[--program->tryCatchStackCount];

        // Pop Loops placed in intermediary frames between the "try-catch block"
        // frame and the "throw" frame. If a "throw" is found in a deep nested
        // function, this ensure all Loops created until there are popped.
        while (&program->frames[program->framesCount - 1] != tryCatch->frame) {
          while (program->loopStackCount > 0 &&
                 program->loopStack[program->loopStackCount - 1].frame ==
                     &program->frames[program->framesCount - 1]) {
            program->loopStackCount--;
          }

          program->framesCount--;
        }

        // Pop Loops placed in the same frame the try-catch block is
        while (program->loopStackCount > 0 &&
               program->loopStack[program->loopStackCount - 1].frame ==
                   tryCatch->frame &&
               program->loopStack[program->loopStackCount - 1].outIp >
                   tryCatch->startIp &&
               program->loopStack[program->loopStackCount - 1].outIp <
                   tryCatch->outIp) {
          program->loopStackCount--;
        }

        // Get back to the closest try-catch block frame and move ip to the
        // start of the catch block statement.
        program->stackTop = tryCatch->frameStackTop;
        program->frame = tryCatch->frame;
        program->frame->ip = tryCatch->catchIp;

        // If the catch block is compiled to receive a param, it should expect
        // the param as a local variable in the stack
        if (tryCatch->hasCatchParameter) {
          push(program, value);
        }

        // This cover an extreme corner case where we throw an enclosed function
        // in a nested scope.
        closeUpValues(program, tryCatch->frameStackTop - 1);
        break;
      }
      case OP_SWITCH: {
        // Stacks a switch block on the stack

        if (program->switchStackCount + 1 == SWITCH_STACK_MAX) {
          runtimeError(program, NULL,
                       "Cant stack more than %d switch-case blocks.",
                       SWITCH_STACK_MAX);
        }

        Switch* switchBlock =
            &program->switchStack[program->switchStackCount++];
        uint16_t outOffset = READ_SHORT();

        switchBlock->expression = &program->stackTop[-1];
        switchBlock->startIp = program->frame->ip;
        switchBlock->outIp = program->frame->ip + outOffset;
        switchBlock->fallThrough = false;
        switchBlock->frame = program->frame;
        break;
      }
      case OP_SWITCH_BREAK: {
        // Break current switch execution a pop any enclosing try catch
        // statement.

        Switch* switchBlock =
            &program->switchStack[program->switchStackCount - 1];

        // Pop any existing try-catch block inside switch block
        while (program->tryCatchStackCount > 0 &&
               program->tryCatchStack[program->tryCatchStackCount - 1].frame ==
                   program->frame &&
               program->tryCatchStack[program->tryCatchStackCount - 1].outIp >
                   switchBlock->startIp &&
               program->tryCatchStack[program->tryCatchStackCount - 1].outIp <
                   switchBlock->outIp) {
          program->tryCatchStackCount--;
        }

        program->frame->ip = switchBlock->outIp;
        program->stackTop = switchBlock->expression + 1;

        closeUpValues(program, switchBlock->expression - 1);
        break;
      }
      case OP_SWITCH_CASE: {
        // The OP_SWITCH_CASE can handle multiple expressions for executing code
        // like this with one instructions:
        //
        // ...
        //    case 1:
        //    case 2:
        //    case 3:
        // ...
        //
        // if switchBlock.fallThrough is true, just pop all case expressions
        // from the stack and continue. Otherwise, compare (while popping from
        // stack) each one of them against the switchBlock.expression. If one
        // case expression is equal to the switchBlock.expression, assign
        // switchBlock.fallThrough to true and continue. If not, just skip to
        // the next case group.

        Switch* switchBlock =
            &program->switchStack[program->switchStackCount - 1];
        uint16_t offset = READ_SHORT();
        bool hasMatch = false;

        if (switchBlock->fallThrough) {
          while (switchBlock->expression != &program->stackTop[-1]) {
            pop(program);
          }

          break;
        }

        while (switchBlock->expression != &program->stackTop[-1]) {
          if (valuesEqual(*switchBlock->expression, pop(program))) {
            hasMatch = true;
          }
        }

        if (hasMatch) {
          switchBlock->fallThrough = true;
        } else {
          program->frame->ip += offset;
        }
        break;
      }
      case OP_SWITCH_END: {
        // During the switch execution, if no case criteria is met, fallTrough
        // is gonna be false. If that's the case, we check to see if there is a
        // default statement, if so, we jump to the default statement and assign
        // fallThrough to true. Otherwise, we just pop the switchBlock from the
        // stack. This handles well:
        //
        //      1. Normal execution when a case criteria is met (whether we
        //      break or not).
        //      2. No case criteria is met and we need to jump back to the
        //      default statement.
        //      3. No case criteria is met and we dont have default statement.

        Switch* switchBlock =
            &program->switchStack[program->switchStackCount - 1];
        uint16_t defaultOffset = READ_SHORT();

        if (!switchBlock->fallThrough && defaultOffset > 0) {
          switchBlock->fallThrough = true;
          program->frame->ip -= defaultOffset;
        } else {
          program->switchStackCount--;
        }
        break;
      }
      case OP_TRUE:
        push(program, BOOL_VAL(true));
        break;
      case OP_FALSE:
        push(program, BOOL_VAL(false));
        break;
      case OP_NIL:
        push(program, NIL_VAL);
        break;
      case OP_GREATER: {
        BINARY_OP(program, BOOL_VAL, >);
        break;
      }
      case OP_LESS: {
        BINARY_OP(program, BOOL_VAL, <);
        break;
      }
      case OP_ADD: {
        if (IS_STRING(peek(program, 0)) && IS_STRING(peek(program, 1))) {
          concatenate(program);
        } else if (IS_NUMBER(peek(program, 0)) && IS_NUMBER(peek(program, 1))) {
          BINARY_OP(program, NUMBER_VAL, +);
        } else if (IS_STRING(peek(program, 0)) || IS_STRING(peek(program, 1))) {
          ObjString* str1 = toString(peek(program, 1));
          ObjString* str2 = toString(peek(program, 0));

          pop(program);
          pop(program);

          push(program, OBJ_VAL(str1));
          push(program, OBJ_VAL(str2));

          concatenate(program);
        } else {
          recoverableRuntimeError(program, "Invalid operands.");
          continue;
        }
        break;
      }
      case OP_SUBTRACT: {
        BINARY_OP(program, NUMBER_VAL, -);
        break;
      }
      case OP_MULTIPLY: {
        BINARY_OP(program, NUMBER_VAL, *);
        break;
      }
      case OP_DIVIDE: {
        BINARY_OP(program, NUMBER_VAL, /);
        break;
      }
      case OP_EQUAL: {
        Value b = pop(program);
        Value a = pop(program);

        push(program, BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_NOT:
        push(program, BOOL_VAL(isFalsey(pop(program))));
        break;
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(program, 0))) {
          recoverableRuntimeError(program, "Operand must be a number.");
          continue;
        }

        push(program, NUMBER_VAL(-AS_NUMBER(pop(program))));
        break;
      }
      case OP_POP: {
        pop(program);
        break;
      }
      case OP_CALL: {
        uint8_t argCount = READ_BYTE();
        if (!callValue(program, peek(program, argCount), argCount)) {
          continue;
        }
        program->frame = &program->frames[program->framesCount - 1];
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(program, OBJ_VAL(closure));
        for (int idx = 0; idx < closure->upvalueCount; idx++) {
          uint8_t index = READ_BYTE();
          uint8_t isLocal = READ_BYTE();

          if (isLocal)
            closure->upvalues[idx] =
                captureUpvalue(program, program->frame->slots + index);
          else
            closure->upvalues[idx] =
                FRAME_AS_CLOSURE(program->frame)->upvalues[index];
        }
        break;
      }
      case OP_CLASS: {
        push(program, OBJ_VAL(newClass(READ_STRING())));
        break;
      }
      case OP_INHERIT: {
        ObjClass* klass = AS_CLASS(pop(program));
        Value superclass = peek(program, 0);

        if (!IS_CLASS(superclass)) {
          recoverableRuntimeError(program, "Superclass must be a class.");
          continue;
        }

        tableAddAllInherintance(&AS_CLASS(superclass)->methods,
                                &klass->methods);
        break;
      }
      case OP_SUPER: {
        ObjClass* klass = AS_CLASS(pop(program));
        Value base = pop(program);
        ObjString* name = READ_STRING();
        Value value;

        if (!classBoundMethod(base, klass, name, &value)) {
          recoverableRuntimeError(program, "Cannot access method '%s'.",
                                  name->chars);
          continue;
        }

        push(program, value);
        break;
      }
      case OP_METHOD: {
        defineMethod(program, READ_STRING());
        break;
      }
      case OP_OBJECT: {
        // push placeholder value where object instance is gonna be stored
        push(program, NIL_VAL);
        callConstructor(program, vm.klass, 0);
        // pop instance from stack in order to have access to the properties
        Value object = pop(program);
        int propertiesCount = READ_BYTE();

        GCWhiteList(AS_OBJ(object));
        while (propertiesCount > 0) {
          // in case GC is called during tableSet, we need to have the property
          // key-value stacked, to prevent it from being collected.
          tableSet(&AS_INSTANCE(object)->properties,
                   AS_STRING(peek(program, 1)), peek(program, 0));
          pop(program);
          pop(program);
          propertiesCount--;
        }
        GCPopWhiteList();

        // after all properties are consumed, push instance to the stack again
        push(program, object);
        break;
      }
      case OP_CLOSE_UPVALUE: {
        closeUpValues(program, program->stackTop - 1);
        pop(program);
        break;
      }
      case OP_EXPORT: {
        ObjString* name = READ_STRING();

        if (!tableSet(&FRAME_AS_MODULE(program->frame)->exports, name,
                      pop(program))) {
          runtimeError(program, NULL,
                       "Already exporting member with name '%s'.", name->chars);
        }

        break;
      }
      case OP_IMPORT: {
        ObjModule* module = AS_MODULE(READ_CONSTANT());

        if (!module->evaluated) {
          // Call module function
          push(program, OBJ_VAL(module->function));
          callModule(program, module);

          program->frame = &program->frames[program->framesCount - 1];
        } else {
          // If resolved, just copy it exports properties
          ObjInstance* exports = newInstance(vm.moduleExportsClass);
          tableAddAll(&module->exports, &exports->properties);
          push(program, OBJ_VAL(exports));
        }

        break;
      }
      case OP_RETURN: {
        Value result = pop(program);

        closeUpValues(program, program->frame->slots);

        if (IS_FRAME_MODULE(program->frame)) {
          // Free module variables namespace and flag as evaluated
          freeTable(&program->frame->namespace);
          FRAME_AS_MODULE(program->frame)->evaluated = true;

          // Copy it exports properties to frame call result
          ObjInstance* exports = (ObjInstance*)GCWhiteList(
              (Obj*)newInstance(vm.moduleExportsClass));
          tableAddAll(&FRAME_AS_MODULE(program->frame)->exports,
                      &exports->properties);
          GCPopWhiteList();
          result = OBJ_VAL(exports);
        }

        // Ensure loop blocks are popped if returned inside them
        while (program->loopStackCount > 0 &&
               program->loopStack[program->loopStackCount - 1].frame ==
                   program->frame) {
          program->loopStackCount--;
        }

        // Ensure switch blocks are popped if returned inside them
        while (program->switchStackCount > 0 &&
               program->switchStack[program->switchStackCount - 1].frame ==
                   program->frame) {
          program->switchStackCount--;
        }

        // Ensure try-catch blocks are popped if returned inside them
        while (program->tryCatchStackCount > 0 &&
               program->tryCatchStack[program->tryCatchStackCount - 1].frame ==
                   program->frame) {
          program->tryCatchStackCount--;
        }

        program->framesCount--;
        if (program->framesCount == 0) {
          pop(program);

          // Worker threads are expected to return something.
          if (IS_WORKER_THREAD()) {
            push(program, result);
          }

          return INTERPRET_OK;
        }

        program->stackTop = program->frame->slots;
        push(program, result);

        program->frame = &program->frames[program->framesCount - 1];
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
  ObjFunction* function =
      (ObjFunction*)GCWhiteList((Obj*)compile(source, absPath));

  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  ObjClosure* closure = newClosure(function);
  GCPopWhiteList();

  push(&vm.program, OBJ_VAL(closure));
  callEntry(&vm.program, closure);

  InterpretResult result = run(&vm.program);
  freeProgram(&vm.program);

  return result;
}
