#include <stdio.h>
#include <stdlib.h>

#include "array.h"
#include "utils.h"
#include "../object.h"

/*
 * Class native methods always receive the calle as the first argument.
 * But the ArgCount IS NOT CHANGED 
 */

Value length(int argCount, Value* args) {
    arityCheck(0, argCount);

    ValueArray array = AS_ARRAY_LIST(*args);
    return NUMBER_VAL(array.count);
}