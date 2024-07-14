#include "object.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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
  object->klass = NULL;

  object->next = vm.objects;
  vm.objects = object;

#ifdef DEBUG_LOG_GC
  printf("%p allocate %lld for %d\n", (void *)object, size, type);
#endif

  return object;
}

ObjBoundNativeFn *newBoundNativeFn(Value base, ObjNativeFn* native) {
  ObjBoundNativeFn *boundNativeFn = ALLOCATE_OBJ(OBJ_BOUND_NATIVE_FN, ObjBoundNativeFn);
  boundNativeFn->base = base;
  boundNativeFn->native = native;
  boundNativeFn->obj.klass = native->obj.klass;

  return boundNativeFn;
}

ObjArray *newArray() {
  ObjArray *array = ALLOCATE_OBJ(OBJ_ARRAY, ObjArray);
  initValueArray(&array->list);
  array->obj.klass = vm.arrayClass;

  return array;
}

ObjModule *newModule(ObjFunction *function) {
  ObjModule *module = ALLOCATE_OBJ(OBJ_MODULE, ObjModule);
  module->function = function;
  module->evaluated = false;
  module->obj.klass = vm.moduleExportsClass;
  initTable(&module->exports);

  return module;
}

ObjBoundMethod *newBoundMethod(Value base, ObjClosure *method) {
  ObjBoundMethod *boundMethod = ALLOCATE_OBJ(OBJ_BOUND_METHOD, ObjBoundMethod);
  boundMethod->base = base;
  boundMethod->method = method;
  boundMethod->obj.klass = method->obj.klass;

  return boundMethod;
}

ObjInstance *newInstance(ObjClass *klass) {
  ObjInstance *instance = ALLOCATE_OBJ(OBJ_INSTANCE, ObjInstance);
  instance->obj.klass = klass;
  initTable(&instance->properties);

  return instance;
}

ObjClass *newClass(ObjString *name) {
  ObjClass *klass = ALLOCATE_OBJ(OBJ_CLASS, ObjClass);
  klass->name = name;
  initTable(&klass->methods);

  // Inheritance from root Class unless if it is the root class being created.
  // awkward statement
  if (vm.klass != NULL) {
    klass->obj.klass = vm.klass;
    // gc 👌
    push(OBJ_VAL(klass));
    tableAddAll(&vm.klass->methods, &klass->methods);
    pop();
  }

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
  closure->obj.klass = function->obj.klass;

  return closure;
}

ObjFunction *newFunction() {
  ObjFunction *function = ALLOCATE_OBJ(OBJ_FUNCTION, ObjFunction);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  function->obj.klass = vm.functionClass;

  initChunk(&function->chunk);

  return function;
}

ObjNativeFn *newNativeFunction(NativeFn function, ObjString* name) {
  ObjNativeFn *nativeFn = ALLOCATE_OBJ(OBJ_NATIVE_FN, ObjNativeFn);
  nativeFn->name = name;
  nativeFn->function = function;
  nativeFn->obj.klass = vm.nativeFunctionClass;
  
  return nativeFn;
}

ObjString *allocateString(char *chars, int length) {
  ObjString *string = ALLOCATE_OBJ(OBJ_STRING, ObjString);
  string->chars = chars;
  string->length = length;
  string->hash = hashString(chars, length);
  string->obj.klass = vm.stringClass;

  // GC 👌
  push(OBJ_VAL(string));
  tableSet(&vm.strings, string, BOOL_VAL(true));
  pop();

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

static void printValueArray(ValueArray* array) {
  printf("[");
  
  for (int idx = 0; idx < array->count; idx++) {
    printfValue(array->values[idx]);
    if (idx < array->count - 1) {
      printf(", ");
    }
  }
  
  printf("]");
}

void printObject(Value value) {
  switch (AS_OBJ(value)->type) {
    case OBJ_BOUND_NATIVE_FN:
      printf("<native fn>");
      break;
    case OBJ_ARRAY:
      printValueArray(&AS_ARRAY(value)->list);
      break;
    case OBJ_MODULE:
      printf("<module %s>", AS_MODULE(value)->function->name->chars);
      break;
    case OBJ_BOUND_METHOD:
      printf("%s", AS_BOUND_METHOD(value)->method->function->name->chars);
      break;
    case OBJ_INSTANCE:
      printf("instance of %s", AS_INSTANCE(value)->obj.klass->name->chars);
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

ObjString* numberToString(double value) {
  if (isnan(value)) {
    return CONSTANT_STRING("NaN");
  }
  if (isinf(value)) {
    if (value > 0) {
      return CONSTANT_STRING("infinity");
    } else {
      return CONSTANT_STRING("-infinity");
    }
  }

  /*
  * The %g format specifier chooses the "best" representation between %e (Decimal scientifier notation) and %f (Decimal floating point number)
  * The longest string generated for 12 points of precision would be in %e, something like this:
  * 
  *   -1.xxxxxxxxxxxxe+yyyy\0
  *  
  * +  1 digit for '-'
  * +  1 digit for '1' digit before decimal point
  * +  1 digit for '.' decimal point 
  * + 12 digits for 'x' mantissa defined in the precision = 12
  * +  1 digit for 'e' expoent char
  * +  1 digit for '+' or '-' expoent char signal
  * +  4 digits for 'y' expoent (the IEE 754 defines 3 digits, but we are arbitraly considering at most 4)
  * +  1 digit for '\0' null terminator
  * = 22 digits
  */
  char buffer[22];
  int len = sprintf(buffer, "%.12g", value);
  return copyString(buffer, len);
}

static ObjString* functionToString(ObjFunction *function) {
  if (function->name == NULL) {
    return CONSTANT_STRING("<script>");
  }
  
  return copyString(function->name->chars, function->name->length);
}

ObjString* objToString(Value value) {
   switch (AS_OBJ(value)->type) {
    case OBJ_BOUND_NATIVE_FN:
      return copyString(AS_BOUND_NATIVE_FN(value)->native->name->chars, AS_BOUND_NATIVE_FN(value)->native->name->length);
    case OBJ_BOUND_METHOD:
      return copyString(AS_BOUND_METHOD(value)->method->function->name->chars, AS_BOUND_METHOD(value)->method->function->name->length);
    case OBJ_CLASS:
      return copyString(AS_CLASS(value)->name->chars, AS_CLASS(value)->name->length);
    case OBJ_STRING:
      return copyString(AS_CSTRING(value), AS_STRING(value)->length);
    case OBJ_FUNCTION:
      return functionToString(AS_FUNCTION(value));
    case OBJ_CLOSURE:
      return functionToString(AS_CLOSURE(value)->function);
    case OBJ_UPVALUE:
      return toString(*AS_UP_VALUE(value)->location);
    case OBJ_NATIVE_FN:
      return copyString(AS_NATIVE(value)->name->chars, AS_NATIVE(value)->name->length);
    case OBJ_ARRAY:
    case OBJ_MODULE:
    case OBJ_INSTANCE: {
      // + 13 comes from template length + '\0' char 
      char buffer[AS_OBJ(value)->klass->name->length + 13];
      int len = sprintf(buffer, "instance of %s", AS_OBJ(value)->klass->name->chars);
      return copyString(buffer, len);
    }
  }

  printf("todo: UNREACHABLE.");
  exit(1);
}

ObjString* toString(Value value) {
  #ifdef NAN_BOXING 
    if (IS_BOOL(value)) {
      return CONSTANT_STRING(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
      return CONSTANT_STRING("nil");
    } else if (IS_NUMBER(value)) {
      return numberToString(AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
      return objToString(value);
    }
  #else
    switch (value.type) {
      case VAL_BOOL:
        return CONSTANT_STRING(AS_BOOL(value) ? "true" : "false");
      case VAL_NUMBER:
        return CONSTANT_STRING("nil");
      case VAL_NIL:
        return numberToString(AS_NUMBER(value));
      case VAL_OBJ:
        return objToString(value);
    }
  #endif

  printf("todo: UNREACHABLE.");
  exit(1);
}