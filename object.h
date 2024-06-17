#ifndef object_h
#define object_h

#include "common.h"
#include "value.h"
#include "chunk.h"

typedef enum { OBJ_STRING, OBJ_FUNCTION } ObjType;

struct Obj {
  ObjType type;
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
  int arity;
  Chunk chunk;
  ObjString* name;
} ObjFunction;

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) (isObjType(value, OBJ_STRING))
#define IS_FUNCTION(value) (isObjType(value, OBJ_FUNCTION))

#define AS_STRING(value) ((ObjString *) AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *) AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *) AS_OBJ(value))->chars)

ObjFunction* newFunction();
ObjString *copyString(const char *chars, int length); 
ObjString *takeString(char *chars, int length); 
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif