#include <stdio.h>
#include <stdlib.h>

#include "array.h"
#include "object.h"

/*
    For class native methods, there i
 */

Value length(int argCount, Value* args) {
    if (argCount != 1) {
        // todo arity resolution
        exit(1);
    }

    ValueArray array = AS_ARRAY_LIST(*args);
    return NUMBER_VAL(array.count);
}