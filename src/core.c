#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "core.h"

#include "value.h"
#include "utils.h"
#include "object.h"
#include "memory.h"


static void __arityCheck(int expected, int received) {
    if (expected == received) return;
    
    fprintf(stderr, "Expected %d arguments but received %d.", expected, received);
    exit(70);
}

static inline void __arityLooseCheck(int expected, int received) {
    if (expected >= received) return;
    
    fprintf(stderr, "Expected at most %d arguments but received %d.", expected, received);
    exit(70);
}

static inline Value __nativeClock(int argCount, Value* args) {
  __arityCheck(0, argCount);
  return NUMBER_VAL(clock() / (double) CLOCKS_PER_SEC);
}

static inline Value __nativeClassToString(int argCount, Value* args) {
  __arityCheck(0, argCount);
  return OBJ_VAL(toString(*args));  
}

static inline Value __nativeArrayLength(int argCount, Value* args) {
    __arityCheck(0, argCount);
    ObjArray* array = AS_ARRAY(*args);
    return NUMBER_VAL(array->list.count);
}

static inline Value __nativeArrayPush(int argCount, Value* args) {
    __arityCheck(1, argCount);

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
    __arityCheck(0, argCount);

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
    __arityCheck(1, argCount);

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
    __arityCheck(0, argCount);

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

static inline Value __nativeArraySlice(int argCount, Value* args) {
    __arityLooseCheck(2, argCount);

    ObjArray* array = AS_ARRAY(*args);
    ObjArray* slicedArray = newArray();
    int start = 0;
    int end = array->list.count;

    if (argCount >= 1) {
        start = AS_NUMBER(*(++args));

        if (start < 0) {
            start = end + start;
        }

        if (argCount == 2) {
            end = AS_NUMBER(*(++args));

            if (end < 0) {
                end = array->list.count + end;
            }
        } 
    }

    for (;start < end; start++) {
        writeValueArray(&slicedArray->list, array->list.values[start]);        
    }

    return OBJ_VAL(slicedArray);
}

static void defineNativeFunction(VM* vm, Table* methods, const char* name, NativeFn function) {
  ObjString* string = copyString(name, strlen(name));
  beginAssemblyLine((Obj *) string); 
  ObjNativeFn* native = newNativeFunction(function, string);
  tableSet(methods, string, OBJ_VAL(native));
  endAssemblyLine();
}

static ObjClass* defineNewClass(const char* name) {
  ObjString* string = copyString(name, strlen(name));
  beginAssemblyLine((Obj *) string);   
  ObjClass* klass = newClass(string);
  endAssemblyLine();

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

  defineNativeFunction(vm, &vm->klass->methods, "toString", __nativeClassToString);
  
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
  defineNativeFunction(vm, &vm->arrayClass->methods, "slice", __nativeArraySlice);
  
  vm->moduleExportsClass = defineNewClass("Exports");
  inherit(vm->moduleExportsClass, vm->klass);

  defineNativeFunction(vm, &vm->global, "clock", __nativeClock);
}