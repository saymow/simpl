#ifndef object_h
#define object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE_FN,
  OBJ_CLOSURE,
  OBJ_UPVALUE
} ObjType;

struct Obj {
  ObjType type;
  struct Obj *next;
};

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  NativeFn function;
} ObjNativeFn;

struct ObjString {
  Obj obj;
  int length;
  uint32_t hash;
  char *chars;
};

typedef struct ObjFunction {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef struct ObjUpValue {
  Obj obj;
  Value *location;
} ObjUpValue;

typedef struct ObjClosure {
  Obj obj;
  int upvalueCount;
  ObjUpValue **upvalues;
  ObjFunction *function;
} ObjClosure;

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) (isObjType(value, OBJ_STRING))
#define IS_FUNCTION(value) (isObjType(value, OBJ_FUNCTION))

#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_NATIVE_FN(value) (((ObjNativeFn *)AS_OBJ(value))->function)

ObjClosure *newClosure(ObjFunction *function);
ObjUpValue *newUpValue(Value *value);
ObjFunction *newFunction();
ObjNativeFn *newNativeFunction(NativeFn function);
ObjString *copyString(const char *chars, int length);
ObjString *takeString(char *chars, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif