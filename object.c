#include "object.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

static uint32_t hashString(const char *key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }
  return hash;
}

#define ALLOCATE_OBJ(objectType, type) \
  (type *)allocateObj(objectType, sizeof(type))

Obj *allocateObj(ObjType type, size_t size) {
  Obj *object = reallocate(NULL, 0, size);
  object->type = type;

  object->next = vm.objects;
  vm.objects = object;

  return object;
}

ObjFunction *newFunction() {
  ObjFunction *function = ALLOCATE_OBJ(OBJ_FUNCTION, ObjFunction);
  function->arity = 0;
  function->name = NULL;

  initChunk(&function->chunk);

  return function;
}

ObjString *allocateString(char *chars, int length) {
  ObjString *string = ALLOCATE_OBJ(OBJ_STRING, ObjString);
  string->chars = chars;
  string->length = length;
  string->hash = hashString(chars, length);

  tableSet(&vm.strings, string, BOOL_VAL(true));

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

static void printFunction(ObjFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
  } else {
    printf("<fn %s>", function->name->chars);
  }
}

void printObject(Value value) {
  switch (AS_OBJ(value)->type) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
  }
}