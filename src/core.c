#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "core.h"

#include "core-inc.h"
#include "value.h"
#include "utils.h"
#include "object.h"
#include "memory.h"

#define ASCII_UPPERCASE_TO_LOWERCASE_OFFSET 32

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

// Ensure the index is in [0, +Infinity]
// 1°) If index in [0, length), it returns index.
// 2°) If index in [-length, 0), it returns length + idx.
// 3°) If none of these conditions are met, it returns 0.
#define UNCAPPED_SAFE_INDEX(idx, length)                                            \
     ((idx) >= 0 ?                                                                  \
        (idx) :                                                                     \
     (-(idx) < (length) ?                                                           \
        (length) + (idx) :                                                          \
        0))

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

#define SAFE_NEGATIVE_INDEX(idx, length)                                            \
    ((idx) >= 0 ?                                                                   \
        (idx) :                                                                     \
        (-(idx) < (length) ?                                                        \
            (length) + (idx) :                                                      \
            0))
      
#define NATIVE_RETURN(value)                                                        \
    do {                                                                            \
      push(value);                                                                  \
      return true;                                                                  \
    } while(false)                                                                  \

static inline void swap(ValueArray* arr, int i, int j) {
    Value tmp = arr->values[i];
    arr->values[i] = arr->values[j];
    arr->values[j] = tmp;
}

static inline bool __nativeClassToString(int argCount, Value* args) {
  NATIVE_RETURN(OBJ_VAL(toString(*args)));
}

static inline bool __nativeArrayLength(int argCount, Value* args) {
  ObjArray* array = AS_ARRAY(*args);
  NATIVE_RETURN(NUMBER_VAL(array->list.count));
}

static inline bool __nativeArrayPush(int argCount, Value* args) {
  ObjArray* array = AS_ARRAY(*args);
  Value value = *(++args);

  if (array->list.capacity < array->list.count + 1) {
    int oldCapacity = array->list.capacity;
    array->list.capacity = GROW_CAPACITY(oldCapacity);
    array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
  }

  array->list.values[array->list.count++] = value;

  NATIVE_RETURN(NUMBER_VAL(array->list.count));
}

static inline bool __nativeArrayPop(int argCount, Value* args) {
  ObjArray* array = AS_ARRAY(*args);

  if (array->list.count == 0) {
    NATIVE_RETURN(NIL_VAL);
  }

  Value value = array->list.values[--array->list.count];

  // todo: idk if this should be handled by the garbage collector
  if (array->list.capacity > ARRAY_INITIAL_CAPACITY && array->list.count / (double) array->list.capacity  < ARRAY_MIN_CAPACITY_RATIO) {
    int oldCapacity = array->list.capacity;
    array->list.capacity = SHRINK_CAPACITY(array->list.capacity);
    array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
  }

  NATIVE_RETURN(value);
}

static inline bool __nativeArrayUnshift(int argCount, Value* args) {
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

  NATIVE_RETURN(NUMBER_VAL(++array->list.count)); 
}

static inline bool __nativeArrayShift(int argCount, Value* args) {
  ObjArray* array = AS_ARRAY(*args);

  if (array->list.count == 0) {
    NATIVE_RETURN(NIL_VAL);
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

  NATIVE_RETURN(value);
}

static inline bool __nativeArraySlice(int argCount, Value* args) {
  ObjArray* array = AS_ARRAY(*args);
  ObjArray* slicedArray = newArray();
  
  // push beforehand to the stack to protect from the GC 
  push(OBJ_VAL(slicedArray));

  int start = 0;
  int end = array->list.count;

  if (argCount >= 1) {
    start = SAFE_CONSUME_NUMBER(args, "start");
    // start is inclusive
    start = UNCAPPED_SAFE_INDEX(start, array->list.count);
    if (argCount == 2) {
      end = SAFE_CONSUME_NUMBER(args, "end");
      // end is exclusive, so we need to be able to access the length-ith positon.
      end = SAFE_INDEX_INCLUSIVE(end, array->list.count);
    } 
  }

  for (;start < end; start++) {
    writeValueArray(&slicedArray->list, array->list.values[start]);        
  }

  return true;
}

static inline bool __nativeArrayIndexOf(int argCount, Value* args) {
  ObjArray* array = AS_ARRAY(*args);
  Value value = *(++args);

  for (int idx = 0; idx < array->list.count; idx++) {
    if (valuesEqual(array->list.values[idx], value)) {
      NATIVE_RETURN(NUMBER_VAL(idx));
    }
  }

  NATIVE_RETURN(NUMBER_VAL(-1));
}

static inline bool __nativeArrayInsert(int argCount, Value* args) {
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

  NATIVE_RETURN(OBJ_VAL(array));
}

static inline bool __nativeArrayJoin(int argCount, Value* args) {
  ObjArray* array = AS_ARRAY(*args);
  ObjString* separator = SAFE_CONSUME_STRING(args, "separator");
  ObjArray* tmpArray = (ObjArray*) GCWhiteList((Obj*) newArray());
  // initialize with separators length
  int length = (array->list.count - 1) * separator->length;

  // increment items length
  for (int idx = 0; idx < array->list.count; idx++) {
    ObjString* str = (ObjString*) GCWhiteList((Obj*) toString(array->list.values[idx]));
    length += str->length;
    writeValueArray(&tmpArray->list, OBJ_VAL(str));
    GCPopWhiteList();
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
  // Pop tmpArray from GC white list
  GCPopWhiteList();

  NATIVE_RETURN(OBJ_VAL(takeString(buffer, length)));
}

static inline bool __nativeStaticArrayIsArray(int argCount, Value* args) {
  Value value = *(++args);
  NATIVE_RETURN(IS_ARRAY(value) ? TRUE_VAL : FALSE_VAL);
}

static inline bool __nativeStaticArrayNew(int argCount, Value* args) {
  ObjArray* array = newArray();
  int length = argCount == 1 ? SAFE_CONSUME_NUMBER(args, "length") : 0;

  // push beforehand to the stack to protect from the GC
  push(OBJ_VAL(array));

  while (array->list.capacity < length) {
    array->list.capacity = GROW_CAPACITY(array->list.capacity);
  }

  array->list.values = GROW_ARRAY(Value, array->list.values, 0, array->list.capacity);

  for (int idx = 0; idx < length; idx++) {
    array->list.values[idx] = NIL_VAL;
  }
  array->list.count = length;
  
  return true;
}

static inline bool __nativeSystemLog(int argCount, Value* args) {
  printValue(*(++args));
  printf("\n");
  NATIVE_RETURN(NIL_VAL);  
}

static inline bool __nativeSystemClock(int argCount, Value* args) {
  NATIVE_RETURN(NUMBER_VAL(clock() / (double) CLOCKS_PER_SEC));
}

static inline bool __nativeStringToUpperCase(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  char buffer[string->length];

  for (int idx = 0; idx < string->length; idx++) {
    if (string->chars[idx] >= 'a' && string->chars[idx] <= 'z') {
      buffer[idx] = string->chars[idx] - ASCII_UPPERCASE_TO_LOWERCASE_OFFSET;
    } else {
      buffer[idx] = string->chars[idx];
    }
  }

  NATIVE_RETURN(OBJ_VAL(copyString(buffer, string->length)));
}

static inline bool __nativeStringToLowerCase(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  char buffer[string->length];

  for (int idx = 0; idx < string->length; idx++) {
    if (string->chars[idx] >= 'A' && string->chars[idx] <= 'Z') {
      buffer[idx] = string->chars[idx] + ASCII_UPPERCASE_TO_LOWERCASE_OFFSET;
    } else {
      buffer[idx] = string->chars[idx];
    }
  }

  NATIVE_RETURN(OBJ_VAL(copyString(buffer, string->length)));
}

static inline bool __nativeStringIncludes(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  ObjString* searchString = SAFE_CONSUME_STRING(args, "searchString");
  int start = 0;

  if (argCount > 1) {
    start = (int) SAFE_CONSUME_NUMBER(args, "start");
    start = SAFE_NEGATIVE_INDEX(start, string->length); 
  }

  if (searchString->length == 0) {
    NATIVE_RETURN(TRUE_VAL);
  }

  for (int i = start; i < string->length; i++) {
    int j = 0;
    while (
      i + j < string->length &&
      j < searchString->length &&
      string->chars[i + j] == searchString->chars[j] 
    ) {
      j++;
    }

    if (j == searchString->length) {
      NATIVE_RETURN(TRUE_VAL);
    }
  }

  NATIVE_RETURN(FALSE_VAL);
}

static inline bool __nativeStringSplit(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  ObjString* separator = SAFE_CONSUME_STRING(args, "separator");
  ObjArray* response = newArray();

  // push beforehand to the stack to protect from the GC 
  push(OBJ_VAL(response));

  int k = 0;  
  for (int i = 0; i < string->length; i++) {
    int j = 0;

    while (
      i + j < string->length &&
      j < separator->length &&
      string->chars[i + j] == separator->chars[j] 
    ) {
      j++;
    }

    // separator found
    if (j == separator->length) {
      ObjString* segment = (ObjString*) GCWhiteList((Obj*) copyString(&string->chars[k], i - k + (separator->length == 0)));
      writeValueArray(&response->list, OBJ_VAL(segment));
      GCPopWhiteList();
      k = i + (separator->length == 0 ? 1 : j);
      i = i + (separator->length == 0 ? 0 : j - 1);
    }
  }

  if (separator->length > 0) {
    writeValueArray(&response->list, OBJ_VAL(copyString(&string->chars[k], string->length - k)));
  }

  return true;
}

static inline bool __nativeStringSubstr(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  int start = 0;
  int end = string->length;

  start = SAFE_CONSUME_NUMBER(args, "startIdx");
  start = SAFE_NEGATIVE_INDEX(start, string->length);

  if (argCount > 1) {
    end = SAFE_CONSUME_NUMBER(args, "endIdx");
    end = SAFE_INDEX_INCLUSIVE(end, string->length);
  }

  int length = (end - start) > 0 ? end - start : 0; 
  char buffer[length];

  for (int idx = start; idx < end; idx++) {
    buffer[idx - start] = string->chars[idx];
  }

  NATIVE_RETURN(OBJ_VAL(copyString(buffer, length)));
}

static inline bool __nativeStringLength(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  NATIVE_RETURN(NUMBER_VAL(string->length));
}

static inline bool __nativeStringEndsWith(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  ObjString* searchString = SAFE_CONSUME_STRING(args, "searchString");
  int start = string->length - searchString->length;

  // searchString length is greater than string length
  if (start < 0) {
    NATIVE_RETURN(FALSE_VAL);
  }

  for (int idx = 0; idx < searchString->length; idx++) {
    if (searchString->chars[idx] != string->chars[start + idx]) {
      NATIVE_RETURN(FALSE_VAL);
    }
  }

  NATIVE_RETURN(TRUE_VAL);
}

static inline bool __nativeStringStarsWith(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  ObjString* searchString = SAFE_CONSUME_STRING(args, "searchString");

  if (searchString->length > string->length) {
    NATIVE_RETURN(FALSE_VAL);
  }

  for (int idx = 0; idx < searchString->length; idx++) {
    if (searchString->chars[idx] != string->chars[idx]) {
      push(FALSE_VAL);
      return true;
    }
  }

  NATIVE_RETURN(TRUE_VAL);
}

static inline bool __nativeStringTrimEnd(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  int idx = string->length - 1;

  while (
    idx >= 0 &&
    string->chars[idx] == ' '
  ) idx--;
  
  char buffer[idx + 2];
  memcpy(buffer, string->chars, idx + 2);
  buffer[idx + 1] = '\0'; 

  NATIVE_RETURN(OBJ_VAL(copyString(buffer, idx + 1)));
}

static inline bool __nativeStringTrimStart(int argCount, Value* args) {
  ObjString* string = AS_STRING(*args);
  int idx = 0;

  while (
    idx < string->length &&
    string->chars[idx] == ' '
  ) idx++;
  
  char buffer[string->length - idx + 1];
  memcpy(buffer, &string->chars[idx], string->length - idx);
  buffer[string->length - idx] = '\0'; 

  NATIVE_RETURN(OBJ_VAL(copyString(buffer, string->length - idx)));
}

static inline bool __nativeStaticStringIsString(int argCount, Value* args) {
  NATIVE_RETURN(IS_STRING(*(++args)) ? TRUE_VAL : FALSE_VAL);
}

static inline bool __nativeStaticStringNew(int argCount, Value* args) {
  if (argCount == 0) {
    NATIVE_RETURN(OBJ_VAL(CONSTANT_STRING("")));  
  }
  
  NATIVE_RETURN(OBJ_VAL(toString(*(++args))));
}

static inline bool __nativeStaticNumberIsNumber(int argCount, Value* args) {
  NATIVE_RETURN(IS_NUMBER(*(++args)) ? TRUE_VAL : FALSE_VAL);
}

static inline bool __nativeStaticNumberToNumber(int argCount, Value* args) {
  ObjString* string = SAFE_CONSUME_STRING(args, "argument");
  char *err_ptr;
  double number = strtod(string->chars, &err_ptr);

  // parse error
  if (*err_ptr != '\0') {
    // to handle exception
  }

  NATIVE_RETURN(NUMBER_VAL(number));
}

static inline bool __nativeStaticNumberToInteger(int argCount, Value* args) {
  Value value = *(++args);

  if (IS_STRING(value)) {
    char *err_ptr;
    double integer = strtol(AS_STRING(value)->chars, &err_ptr, 10);

    // parse error
    if (*err_ptr != '\0') {
      // to handle exception
    }

    NATIVE_RETURN(NUMBER_VAL(integer));
  } else if (IS_NUMBER(value)) {
    int integer = trunc(AS_NUMBER(value));
    
    NATIVE_RETURN(NUMBER_VAL(integer));
  } else {
    push(OBJ_VAL(CONSTANT_STRING("Expected argument to be a string or a number.")));                              
    return false;                                                           
  }
}

static inline bool __nativeStaticMathAbs(int argCount, Value* args) {
  double num = SAFE_CONSUME_NUMBER(args, "argument");
  NATIVE_RETURN(NUMBER_VAL(num < 0 ? -num : num));
}

static inline bool __nativeStaticMathMin(int argCount, Value* args) {
  double num = SAFE_CONSUME_NUMBER(args, "first argument");
  double num2 = SAFE_CONSUME_NUMBER(args, "second argument");
  NATIVE_RETURN(NUMBER_VAL(num < num2 ? num : num2));
}

static inline bool __nativeStaticMathMax(int argCount, Value* args) {
  double num = SAFE_CONSUME_NUMBER(args, "first argument");
  double num2 = SAFE_CONSUME_NUMBER(args, "second argument");
  NATIVE_RETURN(NUMBER_VAL(num > num2 ? num : num2));
}

static inline bool __nativeStaticMathClamp(int argCount, Value* args) {
  double bound = SAFE_CONSUME_NUMBER(args, "lower bound");
  double num = SAFE_CONSUME_NUMBER(args, "argument");
  double bound1 = SAFE_CONSUME_NUMBER(args, "high bound");

  double min = bound < bound1 ? bound : bound1;
  double max = bound > bound1 ? bound : bound1;

  NATIVE_RETURN(NUMBER_VAL(num > max ? max : num < min ? min : num));
}

static inline bool __nativeStaticErrorNew(int argCount, Value* args) {
  ObjInstance* instance = (ObjInstance*) GCWhiteList((Obj*) newInstance(vm.errorClass));
  ObjString* message = (ObjString*) GCWhiteList((Obj*) SAFE_CONSUME_STRING(args, "error message"));
  ObjString* stack = (ObjString*) GCWhiteList((Obj*) stackTrace());

  tableSet(&instance->properties, (ObjString*) GCWhiteList((Obj*) CONSTANT_STRING("message")), OBJ_VAL(message));
  tableSet(&instance->properties, (ObjString*) GCWhiteList((Obj*) CONSTANT_STRING("stack")), OBJ_VAL(stack));
  // Pop "stack" ObjString 
  GCPopWhiteList();
  // Pop "message" ObjString
  GCPopWhiteList();
  // Pop stack ObjString
  GCPopWhiteList();
  // Pop message ObjString  
  GCPopWhiteList();
  // Pop instance ObjInstance  
  GCPopWhiteList();  
  
  NATIVE_RETURN(OBJ_VAL(instance));
}

static void defineNativeFunction(Table* methods, const char* string, NativeFn function, Arity arity) {
  ObjString* name = copyString(string, strlen(string));
  ObjNativeFn* native = newNativeFunction(function, name, arity);
  Value value;

  // Overloading existing method
  if (tableGet(methods, name, &value) && IS_OVERLOADED_METHOD(value)) {
    AS_OVERLOADED_METHOD(value)->as.nativeMethods[arity] = native;
    return;  
  }

  // Creating new method overload and Assign the function to its slot
  ObjOverloadedMethod* overloadedMethod = newNativeOverloadedMethod(name);
  overloadedMethod->as.nativeMethods[arity] = native;
  tableSet(methods, name, OBJ_VAL(overloadedMethod));
}

static ObjClass* defineNewClass(const char* name) {
  ObjString* string = copyString(name, strlen(name));
  ObjClass* klass = newClass(string);

  return klass;
}

static inline void inherit(Obj* obj, ObjClass* superclass) {
  obj->klass = superclass;

  if (IS_CLASS(OBJ_VAL(obj))) {
    tableAddAllInherintance(&superclass->methods, &((ObjClass*) obj)->methods);
  }
}

void initializeCore(VM* vm) {
  //                        System class initialization
  //
  // The order which class are initialized is extremely important to ensure the propper
  // inheritance.  

  vm->klass = NULL;
  vm->metaArrayClass = NULL;
  vm->metaStringClass = NULL;
  vm->metaNumberClass = NULL;
  vm->metaMathClass = NULL;
  vm->metaErrorClass = NULL;
  vm->metaSystemClass = NULL;
  vm->nilClass = NULL;
  vm->boolClass = NULL;
  vm->numberClass = NULL;
  vm->mathClass = NULL;
  vm->stringClass = NULL;
  vm->functionClass = NULL;
  vm->nativeFunctionClass = NULL;
  vm->arrayClass = NULL; 
  vm->errorClass = NULL;
  vm->moduleExportsClass = NULL;
  vm->systemClass = NULL;

  vm->klass = defineNewClass("Class");
  vm->metaStringClass = defineNewClass("MetaString");
  vm->stringClass = defineNewClass("String");
  vm->nativeFunctionClass = defineNewClass("NativeFunction");

  defineNativeFunction(&vm->klass->methods, "toString", __nativeClassToString, ARGS_ARITY_0);
  
  // Class inherits from itself
  vm->klass->obj.klass = vm->klass;

  inherit((Obj *)vm->metaStringClass,  vm->klass);

  defineNativeFunction(&vm->metaStringClass->methods, "isString", __nativeStaticStringIsString, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaStringClass->methods, "new", __nativeStaticStringNew, ARGS_ARITY_0);
  defineNativeFunction(&vm->metaStringClass->methods, "new", __nativeStaticStringNew, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaStringClass->methods, "String", __nativeStaticStringNew, ARGS_ARITY_0);
  defineNativeFunction(&vm->metaStringClass->methods, "String", __nativeStaticStringNew, ARGS_ARITY_1);

  inherit((Obj *)vm->stringClass, vm->metaStringClass);

  defineNativeFunction(&vm->stringClass->methods, "toUpperCase", __nativeStringToUpperCase, ARGS_ARITY_0);
  defineNativeFunction(&vm->stringClass->methods, "toLowerCase", __nativeStringToLowerCase, ARGS_ARITY_0);
  defineNativeFunction(&vm->stringClass->methods, "includes", __nativeStringIncludes, ARGS_ARITY_0);
  defineNativeFunction(&vm->stringClass->methods, "includes", __nativeStringIncludes, ARGS_ARITY_1);
  defineNativeFunction(&vm->stringClass->methods, "split", __nativeStringSplit, ARGS_ARITY_1);
  defineNativeFunction(&vm->stringClass->methods, "substr", __nativeStringSubstr, ARGS_ARITY_1);
  defineNativeFunction(&vm->stringClass->methods, "substr", __nativeStringSubstr, ARGS_ARITY_2);
  defineNativeFunction(&vm->stringClass->methods, "length", __nativeStringLength, ARGS_ARITY_0);
  defineNativeFunction(&vm->stringClass->methods, "endsWith", __nativeStringEndsWith, ARGS_ARITY_1);
  defineNativeFunction(&vm->stringClass->methods, "startsWith", __nativeStringStarsWith, ARGS_ARITY_1);
  defineNativeFunction(&vm->stringClass->methods, "trimEnd", __nativeStringTrimEnd, ARGS_ARITY_0);
  defineNativeFunction(&vm->stringClass->methods, "trimStart", __nativeStringTrimStart, ARGS_ARITY_0);

  inherit((Obj *)vm->nativeFunctionClass, vm->klass);

  inherit((Obj *)vm->klass->name, vm->stringClass);
  inherit((Obj *)vm->metaStringClass->name, vm->stringClass);
  inherit((Obj *)vm->stringClass->name, vm->stringClass);
  inherit((Obj *)vm->nativeFunctionClass->name, vm->stringClass);

  vm->nilClass = defineNewClass("Nil");
  inherit((Obj *)vm->nilClass, vm->klass);

  vm->boolClass = defineNewClass("Bool");
  inherit((Obj *)vm->boolClass, vm->klass);

  vm->metaNumberClass = defineNewClass("MetaNumber");
  inherit((Obj *) vm->metaNumberClass, vm->klass);

  // Define Number static methods
  defineNativeFunction(&vm->metaNumberClass->methods, "isNumber", __nativeStaticNumberIsNumber, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaNumberClass->methods, "toNumber", __nativeStaticNumberToNumber, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaNumberClass->methods, "toInteger", __nativeStaticNumberToInteger, ARGS_ARITY_1);

  vm->numberClass = defineNewClass("Number");
  inherit((Obj *)vm->numberClass, vm->metaNumberClass);

  vm->metaMathClass = defineNewClass("MetaMath");
  inherit((Obj *)vm->metaMathClass, vm->klass);

  // Define Math static methods
  defineNativeFunction(&vm->metaMathClass->methods, "abs", __nativeStaticMathAbs, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaMathClass->methods, "min", __nativeStaticMathMin, ARGS_ARITY_2);
  defineNativeFunction(&vm->metaMathClass->methods, "max", __nativeStaticMathMax, ARGS_ARITY_2);
  defineNativeFunction(&vm->metaMathClass->methods, "clamp", __nativeStaticMathClamp, ARGS_ARITY_3);

  vm->mathClass = defineNewClass("Math");
  inherit((Obj *)vm->mathClass, vm->metaMathClass);

  vm->functionClass = defineNewClass("Function");
  inherit((Obj *)vm->functionClass, vm->klass);

  vm->metaArrayClass = defineNewClass("MetaArray");
  inherit((Obj *)vm->metaArrayClass, vm->klass);

  // Array static methods 
  defineNativeFunction(&vm->metaArrayClass->methods, "isArray", __nativeStaticArrayIsArray, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaArrayClass->methods, "new", __nativeStaticArrayNew, ARGS_ARITY_0);
  defineNativeFunction(&vm->metaArrayClass->methods, "new", __nativeStaticArrayNew, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaArrayClass->methods, "Array", __nativeStaticArrayNew, ARGS_ARITY_0);
  defineNativeFunction(&vm->metaArrayClass->methods, "Array", __nativeStaticArrayNew, ARGS_ARITY_1);

  vm->arrayClass = defineNewClass("Array");
  inherit((Obj *)vm->arrayClass, vm->metaArrayClass);

  // Array methods
  defineNativeFunction(&vm->arrayClass->methods, "length", __nativeArrayLength, ARGS_ARITY_0);
  defineNativeFunction(&vm->arrayClass->methods, "push", __nativeArrayPush, ARGS_ARITY_1);
  defineNativeFunction(&vm->arrayClass->methods, "pop", __nativeArrayPop, ARGS_ARITY_0);
  defineNativeFunction(&vm->arrayClass->methods, "unshift", __nativeArrayUnshift, ARGS_ARITY_1);
  defineNativeFunction(&vm->arrayClass->methods, "shift", __nativeArrayShift, ARGS_ARITY_0);
  defineNativeFunction(&vm->arrayClass->methods, "slice", __nativeArraySlice, ARGS_ARITY_0);
  defineNativeFunction(&vm->arrayClass->methods, "slice", __nativeArraySlice, ARGS_ARITY_1);
  defineNativeFunction(&vm->arrayClass->methods, "slice", __nativeArraySlice, ARGS_ARITY_2);
  defineNativeFunction(&vm->arrayClass->methods, "indexOf", __nativeArrayIndexOf, ARGS_ARITY_1);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_2);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_3);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_4);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_5);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_6);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_7);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_8);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_9);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_10);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_11);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_12);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_13);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_14);
  defineNativeFunction(&vm->arrayClass->methods, "insert", __nativeArrayInsert, ARGS_ARITY_15);
  defineNativeFunction(&vm->arrayClass->methods, "join", __nativeArrayJoin, ARGS_ARITY_1);

  vm->metaErrorClass = defineNewClass("MetaError");
  inherit((Obj *) vm->metaErrorClass, vm->klass);

  defineNativeFunction(&vm->metaErrorClass->methods, "new", __nativeStaticErrorNew, ARGS_ARITY_1);
  defineNativeFunction(&vm->metaErrorClass->methods, "Error", __nativeStaticErrorNew, ARGS_ARITY_1);

  vm->errorClass = defineNewClass("Error");
  inherit((Obj *)vm->errorClass, vm->metaErrorClass);

  vm->moduleExportsClass = defineNewClass("Exports");
  inherit((Obj *)vm->moduleExportsClass, vm->klass);

  vm->metaSystemClass = defineNewClass("MetaSystem");
  inherit((Obj *)vm->metaSystemClass, vm->klass);

  defineNativeFunction(&vm->metaSystemClass->methods, "clock", __nativeSystemClock, ARGS_ARITY_0);
  defineNativeFunction(&vm->metaSystemClass->methods, "log", __nativeSystemLog, ARGS_ARITY_1);

  vm->systemClass = defineNewClass("System");
  inherit((Obj *)vm->systemClass, vm->metaSystemClass);

  vm->state = EXTENDING;

  interpret(coreExtension, NULL);

  tableSet(&vm->global, vm->errorClass->name, OBJ_VAL(vm->errorClass));
  tableSet(&vm->global, vm->stringClass->name, OBJ_VAL(vm->stringClass));
  tableSet(&vm->global, vm->numberClass->name, OBJ_VAL(vm->numberClass));
  tableSet(&vm->global, vm->mathClass->name, OBJ_VAL(vm->mathClass));
  tableSet(&vm->global, vm->arrayClass->name, OBJ_VAL(vm->arrayClass));
  tableSet(&vm->global, vm->systemClass->name, OBJ_VAL(vm->systemClass));
}