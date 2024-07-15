#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "core.h"

#include "value.h"
#include "utils.h"
#include "object.h"
#include "memory.h"


#define SAFE_CONSUME_NUMBER(args, name)                                             \
    (double) (                                                                      \
        IS_NUMBER(*(++args)) ?                                                      \
        AS_NUMBER(*args)     :                                                      \
        ({                                                                          \
            char * buffer = ALLOCATE(char, 64);                                     \
            int length = sprintf(buffer, "Expected %s to be a number.", name);      \
            push(OBJ_VAL(takeString(buffer, length)));                              \
            return false;                                                           \
            0.0;                                                                    \
        })                                                                          \
    )

#define SAFE_CONSUME_STRING(args, name)                                             \
    (ObjString *) (                                                                 \
        IS_STRING(*(++args)) ?                                                      \
        AS_STRING(*args)     :                                                      \
        ({                                                                          \
            char * buffer = ALLOCATE(char, 64);                                     \
            int length = sprintf(buffer, "Expected %s to be a string.", name);      \
            push(OBJ_VAL(takeString(buffer, length)));                              \
            return false;                                                           \
            NULL;                                                                   \
        })                                                                          \
    )                                                                               \

// Ensure the index is in [0, length)
// 1°) If index >= length, it returns length - 1 (capping the index).
// 2°) If index in [0, length), it returns index.
// 3°) If index in [-length, 0), it returns length + idx.
// 4°) If none of these conditions are met, it returns 0.
#define SAFE_INDEX(idx, length)                                                     \
    ((idx) >= (length) ?                                                            \
        (length) - 1 :                                                              \
     ((idx) >= 0 ?                                                                  \
        (idx) :                                                                     \
     (-(idx) < (length) ?                                                           \
        (length) + (idx) :                                                          \
        0)))                     

// Ensure the index is in [0, length]
// 1°) If index >= length, it returns length - 1 (capping the index).
// 2°) If index in [0, length], it returns index.
// 3°) If index in [-length, 0), it returns length + idx.
// 4°) If none of these conditions are met, it returns 0.
#define SAFE_INDEX_INCLUSIVE(idx, length)                                           \
    ((idx) > (length) ?                                                             \
        (length)     :                                                              \
     ((idx) >= 0 ?                                                                  \
        (idx) :                                                                     \
     (-(idx) < (length) ?                                                           \
        (length) + (idx) + 1 :                                                      \
        0)))     

static inline bool __arityEqualCheck(int expected, int received) {
    if (expected == received) return true;
    
    char * buffer = ALLOCATE(char, 64);
    int length = sprintf(buffer, "Expected %d arguments but got %d.", expected, received);
    push(OBJ_VAL(takeString(buffer, length)));

    return false;
}

static inline bool __arityLessThanOrEqualCheck(int expected, int received) {
    if (received <= expected) return true;
    
    char * buffer = ALLOCATE(char, 64);
    int length = sprintf(buffer, "Expected at most %d arguments but got %d.", expected, received);
    push(OBJ_VAL(takeString(buffer, length)));

    return false;
}

static inline bool __arityMoreThanOrEqualCheck(int expected, int received) {
    if (received >= expected) return true;
    
    char * buffer = ALLOCATE(char, 64);
    int length = sprintf(buffer, "Expected at least %d arguments but got %d.", expected, received);
    push(OBJ_VAL(takeString(buffer, length)));

    return false;
}

static inline void swap(ValueArray* arr, int i, int j) {
    Value tmp = arr->values[i];
    arr->values[i] = arr->values[j];
    arr->values[j] = tmp;
}

static inline bool __nativeClock(int argCount, Value* args) {
  if (!__arityEqualCheck(0, argCount)) return false;

  push(NUMBER_VAL(clock() / (double) CLOCKS_PER_SEC));
  
  return true;
}

static inline bool __nativeClassToString(int argCount, Value* args) {
  if (!__arityEqualCheck(0, argCount)) return false;

  push(OBJ_VAL(toString(*args)));
  
  return true;  
}

static inline bool __nativeArrayLength(int argCount, Value* args) {
  if (!__arityEqualCheck(0, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);
  push(NUMBER_VAL(array->list.count));

  return true;
}

static inline bool __nativeArrayPush(int argCount, Value* args) {
  if (!__arityEqualCheck(1, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);
  Value value = *(++args);

  if (array->list.capacity < array->list.count + 1) {
    int oldCapacity = array->list.capacity;
    array->list.capacity = GROW_CAPACITY(oldCapacity);
    array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
  }

  array->list.values[array->list.count++] = value;

  push(NUMBER_VAL(array->list.count));

  return true;
}

static inline bool __nativeArrayPop(int argCount, Value* args) {
  if (!__arityEqualCheck(0, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);

  if (array->list.count == 0) {
    push(NIL_VAL);
    return true;
  }

  Value value = array->list.values[--array->list.count];

  // todo: idk if this should be handled by the garbage collector
  if (array->list.capacity > ARRAY_INITIAL_CAPACITY && array->list.count / (double) array->list.capacity  < ARRAY_MIN_CAPACITY_RATIO) {
    int oldCapacity = array->list.capacity;
    array->list.capacity = SHRINK_CAPACITY(array->list.capacity);
    array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
  }

  push(value);
  
  return true; 
}

static inline bool __nativeArrayUnshift(int argCount, Value* args) {
  if (!__arityEqualCheck(1, argCount)) return false;

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

  push(NUMBER_VAL(++array->list.count)); 

  return true;
}

static inline bool __nativeArrayShift(int argCount, Value* args) {
  if (!__arityEqualCheck(0, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);

  if (array->list.count == 0) {
    push(NIL_VAL);
    return true;
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

  push(value);
  return true; 
}

static inline bool __nativeArraySlice(int argCount, Value* args) {
  if (!__arityLessThanOrEqualCheck(2, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);
  ObjArray* slicedArray = newArray();
  int start = 0;
  int end = array->list.count;

  if (argCount >= 1) {
    start = SAFE_CONSUME_NUMBER(args, "start");
    // stard is inclusive
    start = SAFE_INDEX(start, array->list.count);
    if (argCount == 2) {
      end = SAFE_CONSUME_NUMBER(args, "end");
      // end is exclusive, so we need to be able to access the length-ith positon.
      end = SAFE_INDEX_INCLUSIVE(end, array->list.count);
    } 
  }

  for (;start < end; start++) {
    writeValueArray(&slicedArray->list, array->list.values[start]);        
  }

  push(OBJ_VAL(slicedArray));
  return true;
}

static inline bool __nativeArrayIndexOf(int argCount, Value* args) {
  if (!__arityLessThanOrEqualCheck(1, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);
  Value value = *(++args);

  for (int idx = 0; idx < array->list.count; idx++) {
    if (valuesEqual(array->list.values[idx], value)) {
      push(NUMBER_VAL(idx));
      return true;
    }
  }

  push(NUMBER_VAL(-1));
  return true;
}

static inline bool __nativeArrayInsert(int argCount, Value* args) {
  if (!__arityMoreThanOrEqualCheck(2, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);
  int valuesToInsert = argCount - 1;
  int insertIndex = SAFE_CONSUME_NUMBER(args, "index");
  int arrInitialLen = array->list.count;

  insertIndex = SAFE_INDEX_INCLUSIVE(insertIndex, array->list.count);

  // Allocate space at the end for values to be insert
  for (int idx = 0; idx < valuesToInsert; idx++) {
    writeValueArray(&array->list, NIL_VAL);
  }

  // Shift existing values after insert index to the right by valuesToInsert offset
  for (int idx = 0; idx < arrInitialLen - insertIndex; idx++) {
    swap(&array->list, arrInitialLen - 1 - idx, arrInitialLen - 1 - idx + valuesToInsert);
  }

  // Insert items at their positions    
  for (int idx = 0; idx < valuesToInsert; idx++) {
    array->list.values[idx + insertIndex] = *(++args);
  }

  push(OBJ_VAL(array));
  return true;
}

static inline bool __nativeArrayJoin(int argCount, Value* args) {
  if (!__arityMoreThanOrEqualCheck(1, argCount)) return false;

  ObjArray* array = AS_ARRAY(*args);
  ObjString* separator = SAFE_CONSUME_STRING(args, "separator");
  ObjArray* tmpArray = newArray();
  int length = (array->list.count - 1) * separator->length;

  for (int idx = 0; idx < array->list.count; idx++) {
    ObjString* str = toString(array->list.values[idx]);
    length += str->length;
    writeValueArray(&tmpArray->list, OBJ_VAL(str));
  }

  char *buffer = ALLOCATE(char, length + 1);
  
  // There is no separator after the last element, so iteraate until the last element
  // printing: (element + separator)* + element
  int idx = 0;
  int currentLength = 0;
  for (; idx < tmpArray->list.count - 1; idx++) {
    ObjString* str = ((ObjString *)  AS_OBJ(tmpArray->list.values[idx]));
      
    memcpy(buffer + currentLength, str->chars, str->length);
    memcpy(buffer + currentLength + str->length, separator->chars, separator->length);
    currentLength += str->length + separator->length;
  }

  ObjString* str = ((ObjString *)  AS_OBJ(tmpArray->list.values[idx]));
  memcpy(buffer + currentLength, str->chars, str->length);

  buffer[length] = '\0';

  push(OBJ_VAL(takeString(buffer, length)));
  return true;
}

static inline bool __nativeStaticArrayIsArray(int argCount, Value* args) {
  if (!__arityEqualCheck(1, argCount)) return false;

  Value value = *(++args);
  push(IS_ARRAY(value) ? TRUE_VAL : FALSE_VAL);
  return true;
}

static inline bool __nativeStaticArrayNew(int argCount, Value* args) {
  if (!__arityLessThanOrEqualCheck(1, argCount)) return false;

  ObjArray* array = newArray();
  array->list.count = argCount == 1 ? SAFE_CONSUME_NUMBER(args, "length") : 0;

  while (array->list.capacity < array->list.count) {
    array->list.capacity = GROW_CAPACITY(array->list.capacity);
  }

  array->list.values = GROW_ARRAY(Value, array->list.values, 0, array->list.capacity);
  
  for (int idx = 0; idx < array->list.count; idx++) {
    array->list.values[idx] = NIL_VAL;
  }
  
  push(OBJ_VAL(array));
  return true;
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
  vm->metaArrayClass = NULL;
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

  vm->metaArrayClass = defineNewClass("MetaArray");
  inherit(vm->metaArrayClass, vm->klass);

  defineNativeFunction(vm, &vm->metaArrayClass->methods, "isArray", __nativeStaticArrayIsArray);
  defineNativeFunction(vm, &vm->metaArrayClass->methods, "new", __nativeStaticArrayNew);

  vm->arrayClass = defineNewClass("Array");
  inherit(vm->arrayClass, vm->metaArrayClass);

  defineNativeFunction(vm, &vm->arrayClass->methods, "length", __nativeArrayLength);
  defineNativeFunction(vm, &vm->arrayClass->methods, "push", __nativeArrayPush);
  defineNativeFunction(vm, &vm->arrayClass->methods, "pop", __nativeArrayPop);
  defineNativeFunction(vm, &vm->arrayClass->methods, "unshift", __nativeArrayUnshift);
  defineNativeFunction(vm, &vm->arrayClass->methods, "shift", __nativeArrayShift);
  defineNativeFunction(vm, &vm->arrayClass->methods, "slice", __nativeArraySlice);
  defineNativeFunction(vm, &vm->arrayClass->methods, "indexOf", __nativeArrayIndexOf);
  defineNativeFunction(vm, &vm->arrayClass->methods, "insert", __nativeArrayInsert);
  defineNativeFunction(vm, &vm->arrayClass->methods, "join", __nativeArrayJoin);

  vm->moduleExportsClass = defineNewClass("Exports");
  inherit(vm->moduleExportsClass, vm->klass);

  defineNativeFunction(vm, &vm->global, "clock", __nativeClock);
  
  
  tableSet(&vm->global, vm->arrayClass->name, OBJ_VAL(vm->arrayClass));
}