#include "object.h"

#include <string.h>
#include <stdio.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

void printObject(Value value) {
  switch (AS_OBJ(value)->type) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
  }
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

ObjString *allocateString(char *chars, int length) {
  ObjString *string = ALLOCATE_OBJ(OBJ_STRING, ObjString);
  string->chars = chars;
  string->length = length;

  return string;
}

ObjString *takeString(char *chars, int length) {
  return allocateString(chars, length);
}

ObjString *copyString(const char *chars, int length) {
  char *buffer = ALLOCATE(char, length + 1);
  memcpy(buffer, chars, length);
  buffer[length] = '\0';
  return allocateString(buffer, length);
}