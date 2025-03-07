#include "object.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "utils.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(objectType, type) \
  (type *)allocateObj(objectType, sizeof(type))

Obj *allocateObj(ObjType type, size_t size) {
  // Lock memory allocation area
  pthread_mutex_lock(&vm.memoryAllocationMutex);
  Obj *object = reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;
  object->klass = NULL;

  object->next = vm.objects;
  vm.objects = object;

#ifdef DEBUG_LOG_GC
  printf("%p allocate %ld for %d\n", (void *)object, size, type);
#endif

  // Unlock memory allocation area
  pthread_mutex_unlock(&vm.memoryAllocationMutex);

  return object;
}

ObjBoundOverloadedMethod *newBoundOverloadedMethod(
    Value value, ObjOverloadedMethod *overloadedMethod) {
  ObjBoundOverloadedMethod *boundOverloadedMethod =
      ALLOCATE_OBJ(OBJ_BOUND_OVERLOADED_METHOD, ObjBoundOverloadedMethod);
  boundOverloadedMethod->obj.klass = overloadedMethod->obj.klass;
  boundOverloadedMethod->overloadedMethod = overloadedMethod;
  boundOverloadedMethod->base = value;

  return boundOverloadedMethod;
}

ObjOverloadedMethod *newNativeOverloadedMethod(ObjString *name) {
  ObjOverloadedMethod *overloadedMethod =
      ALLOCATE_OBJ(OBJ_OVERLOADED_METHOD, ObjOverloadedMethod);
  overloadedMethod->obj.klass = vm.nativeFunctionClass;
  overloadedMethod->type = NATIVE_METHOD;
  overloadedMethod->name = name;

  for (int idx = 0; idx < ARGS_ARITY_MAX; idx++) {
    overloadedMethod->as.nativeMethods[idx] = NULL;
  }

  return overloadedMethod;
}

ObjOverloadedMethod *newOverloadedMethod(ObjString *name) {
  ObjOverloadedMethod *overloadedMethod =
      ALLOCATE_OBJ(OBJ_OVERLOADED_METHOD, ObjOverloadedMethod);
  overloadedMethod->obj.klass = vm.functionClass;
  overloadedMethod->type = USER_METHOD;
  overloadedMethod->name = name;

  for (int idx = 0; idx < ARGS_ARITY_MAX; idx++) {
    overloadedMethod->as.userMethods[idx] = NULL;
  }

  return overloadedMethod;
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
  module->resolved = false;
  module->obj.klass = vm.moduleExportsClass;
  module->exports = NIL_VAL;

  return module;
}

ObjModule *newNativeModule(ObjString* moduleName) {
  ObjModule *module = ALLOCATE_OBJ(OBJ_MODULE, ObjModule);
  module->function = NULL;
  module->native = true;
  module->resolved = true;
  module->obj.klass = vm.moduleExportsClass;

  // get native module from table
  Value value;
  tableGet(&vm.modules, moduleName, &value);  

  // copy to exports 
  module->exports = value;

  return module;
}

ObjInstance *newInstance(ObjClass *klass) {
  ObjInstance *instance = ALLOCATE_OBJ(OBJ_INSTANCE, ObjInstance);
  instance->obj.klass = klass;
  initTable(&instance->properties);

  return instance;
}

ObjString* pascalCaseToSnakeCase(ObjString* pascalCase) {
  char buffer[256];

  int j = 0;
  for (int i = 0; i < pascalCase->length; i++) {
    if (pascalCase->chars[i] > 'A' && pascalCase->chars[i] < 'Z') {
      if (i > 0) {
        buffer[j++] = '-';
      }
      buffer[j++] = pascalCase->chars[i] + ASCII_UPPERCASE_TO_LOWERCASE_OFFSET; 
    } else {
      buffer[j++] = pascalCase->chars[i];
    }
  }

  return copyString(buffer, j);
}

ObjClass *newSystemClass(ObjString *name) {
  switch (vm.state) {
    case INITIALIZING: {
      // VM INITIALIZING phase:
      // new classes creation is seen as system classes without a propper inheritance structure. 

      ObjClass *klass = ALLOCATE_OBJ(OBJ_CLASS, ObjClass);
      klass->name = name;
      initTable(&klass->methods);

      return klass;
    }
    case EXTENDING_CORE: {
      // VM EXTENDING_CORE phase:
      // new classes creation is seen as increments to existing core classes.
      // In practice we add methods written in Simpl to classes that contains native methods. 

      if (name->chars == vm.arrayClass->name->chars) {
        return vm.arrayClass;
      } else if (name->chars == vm.stringClass->name->chars) {
        return vm.stringClass;
      } else if (name->chars == vm.errorClass->name->chars) {
        return vm.errorClass;
      } else if (name->chars == vm.mathClass->name->chars) {
        return vm.metaMathClass;
      } 
    }
    case EXTENDING_MODULES: {
      // VM EXTENDING_MODULES phase:
      // new classes creation is seen as new modules to be registred in the native modules table.
      // Classes names are used for the module name after convertion from PascalCase to snake-case. 
      ObjClass *klass = ALLOCATE_OBJ(OBJ_CLASS, ObjClass);
      klass->name = name;
      initTable(&klass->methods);

      klass->obj.klass = vm.klass;
      tableAddAllInherintance(&vm.klass->methods, &klass->methods);

      tableSet(&vm.modules, pascalCaseToSnakeCase(name), OBJ_VAL(klass));

      return klass;
    }
    case INITIALIZED:
  }

  fprintf(stderr, "unreachable.");
  exit(1);
}

ObjClass *newClass(ObjString *name) {
  if (vm.state != INITIALIZED) {
    return newSystemClass(name);
  }

  GCWhiteList((Obj *)name);
  ObjClass *klass =
      (ObjClass *)GCWhiteList((Obj *)ALLOCATE_OBJ(OBJ_CLASS, ObjClass));
  klass->name = name;
  initTable(&klass->methods);

  klass->obj.klass = vm.klass;
  tableAddAllInherintance(&vm.klass->methods, &klass->methods);
  GCPopWhiteList();
  GCPopWhiteList();

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
  // since this is not tracked by the vm, there is no risk of the GC running and cleaning this. 
  // But, we cannot allocate this after allocating the ObjClosure, because the ALLOCATE macro, internally,
  // can trigger the GC. Garbage Collector 👌
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
  function->arity = ARGS_ARITY_0;
  function->upvalueCount = 0;
  function->name = NULL;
  function->obj.klass = vm.functionClass;

  initChunk(&function->chunk);

  return function;
}

ObjNativeFn *newNativeFunction(NativeFn function, ObjString *name,
                               Arity arity) {
  ObjNativeFn *nativeFn = ALLOCATE_OBJ(OBJ_NATIVE_FN, ObjNativeFn);
  nativeFn->name = name;
  nativeFn->arity = arity;
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

  GCWhiteList((Obj *)string);
  tableSet(&vm.strings, string, BOOL_VAL(true));
  GCPopWhiteList();

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
  } else if (function->name == vm.lambdaFunctionName) {
    printf("<lambda fn>");
  } else {
    printf("<%s fn>", function->name->chars);
  }
}

static void printValueArray(ValueArray *array) {
  printf("[");

  for (int idx = 0; idx < array->count; idx++) {
    printValue(array->values[idx]);
    if (idx < array->count - 1) {
      printf(", ");
    }
  }

  printf("]");
}

void printObject(Value value) {
  switch (AS_OBJ(value)->type) {
    case OBJ_BOUND_OVERLOADED_METHOD:
      printf("<%s fn>",
             AS_BOUND_OVERLOADED_METHOD(value)->overloadedMethod->name->chars);
      break;
    case OBJ_OVERLOADED_METHOD:
      printf("<%s fn>", AS_OVERLOADED_METHOD(value)->name->chars);
      break;
    case OBJ_ARRAY:
      printValueArray(&AS_ARRAY(value)->list);
      break;
    case OBJ_MODULE:
      ObjModule* module = AS_MODULE(value);
      if (module->native) {
        printf("<native module>");
      } else {
        printf("<%s module>", AS_MODULE(value)->function->name->chars);
      }
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

ObjString *numberToString(double value) {
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
   * The %g format specifier chooses the "best" representation between %e
   * (Decimal scientifier notation) and %f (Decimal floating point number) The
   * longest string generated for 12 points of precision would be in %e,
   * something like this:
   *
   *   -1.xxxxxxxxxxxxe+yyyy\0
   *
   * +  1 digit for '-'
   * +  1 digit for '1' digit before decimal point
   * +  1 digit for '.' decimal point
   * + 12 digits for 'x' mantissa defined in the precision = 12
   * +  1 digit for 'e' expoent char
   * +  1 digit for '+' or '-' expoent char signal
   * +  4 digits for 'y' expoent (the IEE 754 defines 3 digits, but we are
   * arbitraly considering at most 4)
   * +  1 digit for '\0' null terminator
   * = 22 digits
   */
  char buffer[22];
  int len = sprintf(buffer, "%.12g", value);
  return copyString(buffer, len);
}

static ObjString *functionToString(ObjFunction *function) {
  if (function->name == NULL) {
    return CONSTANT_STRING("<script>");
  }

  return copyString(function->name->chars, function->name->length);
}

ObjString *objToString(Value value) {
  switch (AS_OBJ(value)->type) {
    case OBJ_BOUND_OVERLOADED_METHOD:
      return copyString(
          AS_BOUND_OVERLOADED_METHOD(value)->overloadedMethod->name->chars,
          AS_BOUND_OVERLOADED_METHOD(value)->overloadedMethod->name->length);
    case OBJ_OVERLOADED_METHOD:
      return copyString(AS_OVERLOADED_METHOD(value)->name->chars,
                        AS_OVERLOADED_METHOD(value)->name->length);
    case OBJ_CLASS:
      return copyString(AS_CLASS(value)->name->chars,
                        AS_CLASS(value)->name->length);
    case OBJ_STRING:
      return copyString(AS_CSTRING(value), AS_STRING(value)->length);
    case OBJ_FUNCTION:
      return functionToString(AS_FUNCTION(value));
    case OBJ_CLOSURE:
      return functionToString(AS_CLOSURE(value)->function);
    case OBJ_UPVALUE:
      return toString(*AS_UP_VALUE(value)->location);
    case OBJ_NATIVE_FN:
      return copyString(AS_NATIVE(value)->name->chars,
                        AS_NATIVE(value)->name->length);
    case OBJ_ARRAY:
    case OBJ_MODULE:
    case OBJ_INSTANCE: {
      // + 13 comes from template length + '\0' char
      char buffer[AS_OBJ(value)->klass->name->length + 13];
      int len =
          sprintf(buffer, "instance of %s", AS_OBJ(value)->klass->name->chars);
      return copyString(buffer, len);
    }
  }

  printf("todo: UNREACHABLE.");
  exit(1);
}

ObjString *toString(Value value) {
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
      return numberToString(AS_NUMBER(value));
    case VAL_NIL:
      return CONSTANT_STRING("nil");
    case VAL_OBJ:
      return objToString(value);
  }
#endif

  printf("todo: UNREACHABLE.");
  exit(1);
}