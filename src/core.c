#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "core.h"

#include "value.h"
#include "utils.h"
#include "object.h"
#include "memory.h"


static inline void __nativeArityCheck(int expected, int received) {
    if (expected == received) return;
    
    printf("Expected %d arguments but received %d.", expected, received);
    exit(70);
}

static inline Value __nativeToString(int argCount, Value* args) {
  __nativeArityCheck(0, argCount);
  return OBJ_VAL(toString(*args));  
}

static inline Value __nativeClock(int argCount, Value* args) {
  __nativeArityCheck(0, argCount);
  return NUMBER_VAL(clock() / (double) CLOCKS_PER_SEC);
}

static inline Value __nativeArrayLength(int argCount, Value* args) {
    __nativeArityCheck(0, argCount);
    ObjArray* array = AS_ARRAY(*args);
    return NUMBER_VAL(array->list.count);
}

static inline Value __nativeArrayPush(int argCount, Value* args) {
    __nativeArityCheck(1, argCount);

    ObjArray* array = AS_ARRAY(*args);
    Value value = *(++args);

    if (array->list.capacity < array->list.count + 1) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = GROW_CAPACITY(oldCapacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    array->list.values[array->list.count++] = value;

    return NUMBER_VAL(array->list.count);
}

static inline Value __nativeArrayPop(int argCount, Value* args) {
    __nativeArityCheck(0, argCount);

    ObjArray* array = AS_ARRAY(*args);

    if (array->list.count == 0) {
        return NIL_VAL;
    }

    Value value = array->list.values[--array->list.count];

    // todo: idk if this should be handled by the garbage collector
    if (array->list.capacity > ARRAY_INITIAL_CAPACITY && array->list.count / (double) array->list.capacity  < ARRAY_MIN_CAPACITY_RATIO) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = SHRINK_CAPACITY(array->list.capacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    return value; 
}

static inline Value __nativeArrayUnshift(int argCount, Value* args) {
    __nativeArityCheck(1, argCount);

    ObjArray* array = AS_ARRAY(*args);
    Value value = *(++args);

    if (array->list.capacity < array->list.count + 1) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = GROW_CAPACITY(oldCapacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    for (int idx = array->list.count; idx > 0; idx--) {
        array->list.values[idx] = array->list.values[idx - 1];
    }
    
    array->list.values[0] = value;

    return NUMBER_VAL(++array->list.count); 
}

static inline Value __nativeArrayShift(int argCount, Value* args) {
    __nativeArityCheck(0, argCount);

    ObjArray* array = AS_ARRAY(*args);

    if (array->list.count == 0) {
        return NIL_VAL;
    }

    Value value = array->list.values[0];
    array->list.count--;

    for (int idx = 0; idx < array->list.count; idx++) {
        array->list.values[idx] = array->list.values[idx + 1];
    }

    // todo: idk if this should be handled by the garbage collector
    if (array->list.capacity > ARRAY_INITIAL_CAPACITY && array->list.count / (double) array->list.capacity  < ARRAY_MIN_CAPACITY_RATIO) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = SHRINK_CAPACITY(array->list.capacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    return value; 
}

static void defineNativeFunction(VM* vm, Table* methods, const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, strlen(name))));
  push(OBJ_VAL(newNativeFunction(function, AS_STRING(peek(0)))));
  tableSet(methods, AS_STRING(vm->stack[0]), vm->stack[1]);
  pop();
  pop();
}

static ObjClass* defineNewClass(const char* name) {
  ObjClass* klass;
  push(OBJ_VAL(copyString(name, strlen(name))));  
  klass = newClass(AS_STRING(peek(0)));
  pop();
  return klass;
}

static inline void inherit(ObjClass* subclass, ObjClass* superclass) {
  subclass->obj.klass = superclass;
  tableAddAll(&superclass->methods, &subclass->methods);
}

void initializeCore(VM* vm) {
  vm->klass = NULL;
  vm->nativeFunctionClass = NULL;
  vm->nilClass = NULL;
  vm->boolClass = NULL;
  vm->numberClass = NULL;
  vm->stringClass = NULL;
  vm->functionClass = NULL;
  vm->moduleExportsClass = NULL;
  vm->arrayClass = NULL;

  vm->klass = defineNewClass("Class");

  /*
  * "NativeFunction" needs to be defined second and before "Class" methods definition, since:
  *     
  *     1. It should extend the "Class" that is defined first
  *     2. All "ObjNativeFn" extends to "NativeFunction"
  */  
  vm->nativeFunctionClass = defineNewClass("NativeFunction");

  defineNativeFunction(vm, &vm->klass->methods, "toString", __nativeToString);
  
  // But the "NativeFunction" inheritance should come after the "Class" methods are defined
  inherit(vm->nativeFunctionClass, vm->klass);  

  vm->nilClass = defineNewClass("Nil");
  inherit(vm->nilClass, vm->klass);

  vm->boolClass = defineNewClass("Bool");
  inherit(vm->boolClass, vm->klass);

  vm->numberClass = defineNewClass("Number");
  inherit(vm->numberClass, vm->klass);  

  vm->stringClass = defineNewClass("String");
  inherit(vm->stringClass, vm->klass);  

  vm->functionClass = defineNewClass("Function");
  inherit(vm->functionClass, vm->klass);

  vm->arrayClass = defineNewClass("Array");
  inherit(vm->arrayClass, vm->klass);

  defineNativeFunction(vm, &vm->arrayClass->methods, "length", __nativeArrayLength);
  defineNativeFunction(vm, &vm->arrayClass->methods, "push", __nativeArrayPush);
  defineNativeFunction(vm, &vm->arrayClass->methods, "pop", __nativeArrayPop);
  defineNativeFunction(vm, &vm->arrayClass->methods, "unshift", __nativeArrayUnshift);
  defineNativeFunction(vm, &vm->arrayClass->methods, "shift", __nativeArrayShift);
  
  vm->moduleExportsClass = defineNewClass("Exports");
  inherit(vm->moduleExportsClass, vm->klass);

  defineNativeFunction(vm, &vm->global, "clock", __nativeClock);
}