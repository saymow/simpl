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
  OBJ_CLOSURE,
  OBJ_UPVALUE,
  OBJ_CLASS,
  OBJ_INSTANCE,
  OBJ_BOUND_METHOD,
  OBJ_MODULE,
  OBJ_ARRAY,
  OBJ_BOUND_NATIVE_FN
} ObjType;

struct Obj {
  ObjType type;
  bool isMarked;
  struct Obj *next;
};

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  ObjString *name;
  Table methods;
} ObjClass;

typedef struct {
  Obj obj;
  ObjClass *klass;
  Table properties;
} ObjInstance;

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
  Value closed;
  struct ObjUpValue *next;
} ObjUpValue;

typedef struct ObjClosure {
  Obj obj;
  int upvalueCount;
  ObjUpValue **upvalues;
  ObjFunction *function;
} ObjClosure;

typedef struct ObjBoundMethod {
  Obj obj;
  Value base;
  ObjClosure *method;
} ObjBoundMethod;

typedef struct ObjModule {
  Obj obj;
  ObjFunction *function;
  bool evaluated;
  Table exports;
} ObjModule;

typedef struct ObjArray {
  Obj obj;
  ObjClass *klass;
  ValueArray list;
} ObjArray;

typedef struct ObjBoundNativeFn {
  Obj obj;
  Value base;
  ObjNativeFn *native;
} ObjBoundNativeFn;

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_NATIVE_FN(value) (isObjType(value, OBJ_BOUND_NATIVE_FN))
#define IS_ARRAY(value) (isObjType(value, OBJ_ARRAY))
#define IS_MODULE(value) (isObjType(value, OBJ_MODULE))
#define IS_BOUND_METHOD(value) (isObjType(value, OBJ_BOUND_METHOD))
#define IS_INSTANCE(value) (isObjType(value, OBJ_INSTANCE))
#define IS_CLASS(value) (isObjType(value, OBJ_CLASS))
#define IS_STRING(value) (isObjType(value, OBJ_STRING))
#define IS_FUNCTION(value) (isObjType(value, OBJ_FUNCTION))
#define IS_NATIVE_FUNCTION(value) (isObjType(value, OBJ_NATIVE_FN))

#define AS_BOUND_NATIVE_FN(value) ((ObjBoundNativeFn*)AS_OBJ(value))
#define AS_ARRAY_LIST(value) (((ObjArray *)AS_OBJ(value))->list)
#define AS_ARRAY(value) ((ObjArray *)AS_OBJ(value))
#define AS_MODULE(value) ((ObjModule *)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNativeFn *)AS_OBJ(value)))
#define AS_NATIVE_FN(value) (((ObjNativeFn *)AS_OBJ(value))->function)

ObjBoundNativeFn *newBoundNativeFn(Value base, ObjNativeFn* native);
ObjArray *newArray();
ObjModule *newModule(ObjFunction *function);
ObjBoundMethod *newBoundMethod(Value base, ObjClosure *method);
ObjInstance *newInstance(ObjClass *klass);
ObjClass *newClass(ObjString *name);
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