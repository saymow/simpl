#include "object.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "vm.h"
#include "utils.h"

#define ALLOCATE_OBJ(objectType, type) \
  (type *)allocateObj(objectType, sizeof(type))

Obj *allocateObj(ObjType type, size_t size) {
  Obj *object = reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;

  object->next = vm.objects;
  vm.objects = object;

#ifdef DEBUG_LOG_GC
  printf("%p allocate %lld for %d\n", (void *)object, size, type);
#endif

  return object;
}

ObjBoundMethod *newBoundMethod(Value base, ObjClosure *method) {
  ObjBoundMethod *boundMethod = ALLOCATE_OBJ(OBJ_BOUND_METHOD, ObjBoundMethod);
  boundMethod->base = base;
  boundMethod->method = method;

  return boundMethod;
}

ObjInstance *newInstance(ObjClass *klass) {
  ObjInstance *instance = ALLOCATE_OBJ(OBJ_INSTANCE, ObjInstance);
  instance->klass = klass;
  initTable(&instance->properties);

  return instance;
}

ObjClass *newClass(ObjString *name) {
  ObjClass *klass = ALLOCATE_OBJ(OBJ_CLASS, ObjClass);
  klass->name = name;
  initTable(&klass->methods);

  return klass;
}

ObjUpValue *newUpValue(Value *value) {
  ObjUpValue *upValue = ALLOCATE_OBJ(OBJ_UPVALUE, ObjUpValue);
  upValue->location = value;
  upValue->closed = NIL_VAL;
  upValue->next = NULL;

  return upValue;
}

ObjClosure *newClosure(ObjFunction *function) {
  // Garbage Collector 👌
  ObjUpValue **upvalues = ALLOCATE(ObjUpValue *, function->upvalueCount);
  for (int idx = 0; idx < function->upvalueCount; idx++) {
    upvalues[idx] = NULL;
  }

  ObjClosure *closure = ALLOCATE_OBJ(OBJ_CLOSURE, ObjClosure);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;

  return closure;
}

ObjFunction *newFunction() {
  ObjFunction *function = ALLOCATE_OBJ(OBJ_FUNCTION, ObjFunction);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;

  initChunk(&function->chunk);

  return function;
}

ObjNativeFn *newNativeFunction(NativeFn function) {
  ObjNativeFn *nativeFn = ALLOCATE_OBJ(OBJ_NATIVE_FN, ObjNativeFn);
  nativeFn->function = function;
  return nativeFn;
}

ObjString *allocateString(char *chars, int length) {
  ObjString *string = ALLOCATE_OBJ(OBJ_STRING, ObjString);
  string->chars = chars;
  string->length = length;
  string->hash = hashString(chars, length);

  tableSet(&vm.strings, string, BOOL_VAL(true));

  return string;
}

ObjString *takeString(char *chars, int length) {
  int32_t hash = hashString(chars, length);
  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

  if (interned != NULL) {
    FREE_ARRAY(char, chars, length);
    return interned;
  }

  return allocateString(chars, length);
}

ObjString *copyString(const char *chars, int length) {
  int32_t hash = hashString(chars, length);
  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

  if (interned != NULL) {
    return interned;
  }

  char *buffer = ALLOCATE(char, length + 1);
  memcpy(buffer, chars, length);
  buffer[length] = '\0';
  return allocateString(buffer, length);
}

static void printFunction(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
  } else {
    printf("<fn %s>", function->name->chars);
  }
}

void printObject(Value value) {
  switch (AS_OBJ(value)->type) {
     case OBJ_BOUND_METHOD:
      printf("%s", AS_BOUND_METHOD(value)->method->function->name->chars);
      break;
    case OBJ_INSTANCE:
      printf("instance of %s", AS_INSTANCE(value)->klass->name->chars);
      break;
    case OBJ_CLASS:
      printf("class %s", AS_CLASS(value)->name->chars);
      break;
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
    case OBJ_NATIVE_FN:
      printf("<native fn>");
      break;
    case OBJ_CLOSURE:
      printFunction(AS_CLOSURE(value)->function);
      break;
    case OBJ_UPVALUE:
      printf("<up value>");
      break;
  }
}