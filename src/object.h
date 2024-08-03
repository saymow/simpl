#ifndef object_h
#define object_h

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE_FN,
  OBJ_CLASS,
  OBJ_ARRAY,
  OBJ_INSTANCE,
  OBJ_MODULE,
  OBJ_CLOSURE,
  OBJ_UPVALUE,
  OBJ_OVERLOADED_METHOD,
  OBJ_BOUND_OVERLOADED_METHOD
} ObjType;

typedef enum { USER_METHOD, NATIVE_METHOD } MethodType;

typedef enum {
  ARGS_ARITY_0,
  ARGS_ARITY_1,
  ARGS_ARITY_2,
  ARGS_ARITY_3,
  ARGS_ARITY_4,
  ARGS_ARITY_5,
  ARGS_ARITY_6,
  ARGS_ARITY_7,
  ARGS_ARITY_8,
  ARGS_ARITY_9,
  ARGS_ARITY_10,
  ARGS_ARITY_11,
  ARGS_ARITY_12,
  ARGS_ARITY_13,
  ARGS_ARITY_14,
  ARGS_ARITY_15,
} Arity;

#define ARGS_ARITY_MAX ARGS_ARITY_15 + 1

typedef struct ObjClass ObjClass;

struct Obj {
  ObjType type;
  bool isMarked;
  ObjClass* klass;
  struct Obj *next;
};

struct ObjString {
  Obj obj;
  int length;
  uint32_t hash;
  char *chars;
};

typedef struct ObjFunction {
  Obj obj;
  Arity arity;
  int upvalueCount;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef struct ObjUpValue {
  Obj obj;
  Value *location;
  Value closed;
  struct ObjUpValue *next;
} ObjUpValue;

typedef struct ObjClosure {
  Obj obj;
  int upvalueCount;
  ObjUpValue **upvalues;
  ObjFunction *function;
} ObjClosure;

typedef bool (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  ObjString* name;
  Arity arity;
  NativeFn function;
} ObjNativeFn;

typedef struct ObjOverloadedMethod {
  Obj obj;
  ObjString* name;
  MethodType type;
  union {
    ObjClosure* userMethods[ARGS_ARITY_MAX];
    ObjNativeFn* nativeMethods[ARGS_ARITY_MAX];
  } as;
} ObjOverloadedMethod;

typedef struct ObjBoundOverloadedMethod {
  Obj obj;
  Value base;
  ObjOverloadedMethod* overloadedMethod;
} ObjBoundOverloadedMethod;

struct ObjClass {
  Obj obj;
  ObjString *name;
  Table methods;
};

typedef struct {
  Obj obj;
  Table properties;
} ObjInstance;

typedef struct ObjModule {
  Obj obj;
  ObjFunction *function;
  bool evaluated;
  Table exports;
} ObjModule;

typedef struct ObjArray {
  Obj obj;
  ValueArray list;
} ObjArray;


#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_OVERLOADED_METHOD(value) (isObjType(value, OBJ_BOUND_OVERLOADED_METHOD))
#define IS_OVERLOADED_METHOD(value) (isObjType(value, OBJ_OVERLOADED_METHOD))
#define IS_CLOSURE(value) (isObjType(value, OBJ_CLOSURE))
#define IS_ARRAY(value) (isObjType(value, OBJ_ARRAY))
#define IS_MODULE(value) (isObjType(value, OBJ_MODULE))
#define IS_INSTANCE(value) (isObjType(value, OBJ_INSTANCE))
#define IS_CLASS(value) (isObjType(value, OBJ_CLASS))
#define IS_STRING(value) (isObjType(value, OBJ_STRING))
#define IS_FUNCTION(value) (isObjType(value, OBJ_FUNCTION))
#define IS_NATIVE_FUNCTION(value) (isObjType(value, OBJ_NATIVE_FN))

#define AS_USER_BOUND_OVERLOADED_METHOD(value, arity) (((ObjBoundOverloadedMethod *)AS_OBJ(value))->overloadedMethod->as.userMethods[arity])
#define AS_NATIVE_BOUND_OVERLOADED_METHOD(value, arity) (((ObjBoundOverloadedMethod *)AS_OBJ(value))->overloadedMethod->as.nativeMethods[arity])
#define AS_BOUND_OVERLOADED_METHOD(value) ((ObjBoundOverloadedMethod *)AS_OBJ(value))
#define AS_OVERLOADED_METHOD(value) ((ObjOverloadedMethod *)AS_OBJ(value))
#define AS_UP_VALUE(value) ((ObjUpValue*)AS_OBJ(value))
#define AS_ARRAY_LIST(value) (((ObjArray *)AS_OBJ(value))->list)
#define AS_ARRAY(value) ((ObjArray *)AS_OBJ(value))
#define AS_MODULE(value) ((ObjModule *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNativeFn *)AS_OBJ(value)))
#define AS_NATIVE_FN(value) (((ObjNativeFn *)AS_OBJ(value))->function)


//Expands to copyString in compilation time with the correct string literal length.
//"- 1" is used to remove null terminator char '\0'.
#define CONSTANT_STRING(str) copyString((str), sizeof(str) - 1) 

ObjBoundOverloadedMethod* newBoundOverloadedMethod(Value base, ObjOverloadedMethod* overloadedMethod);
ObjOverloadedMethod* newNativeOverloadedMethod(ObjString* name);
ObjOverloadedMethod* newOverloadedMethod(ObjString* name);
ObjString* toString(Value value);
ObjArray *newArray();
ObjModule *newModule(ObjFunction *function);
ObjInstance *newInstance(ObjClass *klass);
ObjClass *newClass(ObjString *name);
ObjClosure *newClosure(ObjFunction *function);
ObjUpValue *newUpValue(Value *value);
ObjFunction *newFunction();
ObjNativeFn *newNativeFunction(NativeFn function, ObjString* name, Arity arity);
ObjString *copyString(const char *chars, int length);
ObjString *takeString(char *chars, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif