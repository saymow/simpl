#ifndef value_h
#define value_h

#include "common.h"

typedef enum { VAL_BOOL, VAL_NUMBER, VAL_NIL } ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
  } as;
} Value;

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define BOOL_VAL(value) ((Value){.type = VAL_BOOL, {.boolean = value}})
#define NUMBER_VAL(value) ((Value){.type = VAL_NUMBER, {.number = value}})
#define NIL_VAL ((Value){.type = VAL_NIL, {.number = 0}})

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printfValue(Value value);

#endif