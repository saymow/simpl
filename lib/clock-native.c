
#include "clock-native.h"

#include <time.h>

Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL(clock() / CLOCKS_PER_SEC);
}