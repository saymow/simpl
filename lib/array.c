#include <stdio.h>
#include <stdlib.h>

#include "array.h"
#include "utils.h"
#include "../object.h"
#include "../memory.h"

/*
 * Class native methods always receive the calle as the first argument.
 * But the ArgCount IS NOT CHANGED 
 */

Value arrayLength(int argCount, Value* args) {
    arityCheck(0, argCount);

    ObjArray* array = AS_ARRAY(*args);

    return NUMBER_VAL(array->list.count);
}

Value arrayPush(int argCount, Value* args) {
    arityCheck(1, argCount);

    ObjArray* array = AS_ARRAY(*args);
    Value value = *(++args);

    if (array->list.capacity < array->list.count + 1) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = GROW_CAPACITY(oldCapacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    array->list.values[array->list.count++] = value;

    return NUMBER_VAL(array->list.count);
}

Value arrayPop(int argCount, Value* args) {
    arityCheck(0, argCount);

    ObjArray* array = AS_ARRAY(*args);

    return array->list.values[--array->list.count]; 
}