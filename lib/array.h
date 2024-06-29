#ifndef array_h
#define array_h

#include "common.h"
#include "value.h"

Value arrayLength(int argCount, Value* args);
Value arrayPush(int argCount, Value* args);
Value arrayPop(int argCount, Value* args);
Value arrayUnshift(int argCount, Value* args);
Value arrayShift(int argCount, Value* args);

#endif