#ifndef array_h
#define array_h

#include "common.h"
#include "value.h"

Value arrayLength(int argCount, Value* args);
Value arrayPush(int argCount, Value* args);

#endif