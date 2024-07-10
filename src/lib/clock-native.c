
#include "clock-native.h"
#include "utils.h"

#include <time.h>

Value clockNative(int argCount, Value* args) {
  arityCheck(0, argCount);
  
  return NUMBER_VAL(clock() / (double) CLOCKS_PER_SEC);
}