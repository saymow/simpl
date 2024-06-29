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
    Value value = array->list.values[--array->list.count];

    // todo: idk if this should be handled by the garbage collector
    if (array->list.capacity > ARRAY_INITIAL_CAPACITY && array->list.count / (double) array->list.capacity  < ARRAY_MIN_CAPACITY_RATIO) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = SHRINK_CAPACITY(array->list.capacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    return value; 
}

Value arrayUnshift(int argCount, Value* args) {
    arityCheck(1, argCount);

    ObjArray* array = AS_ARRAY(*args);
    Value value = *(++args);

    if (array->list.capacity < array->list.count + 1) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = GROW_CAPACITY(oldCapacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    for (int idx = array->list.count; idx > 0; idx--) {
        array->list.values[idx] = array->list.values[idx - 1];
    }
    
    array->list.values[0] = value;

    return NUMBER_VAL(++array->list.count); 
}

Value arrayShift(int argCount, Value* args) {
    arityCheck(0, argCount);

    ObjArray* array = AS_ARRAY(*args);
    Value value = array->list.values[0];
    array->list.count--;

    for (int idx = 0; idx < array->list.count; idx++) {
        array->list.values[idx] = array->list.values[idx + 1];
    }

    // todo: idk if this should be handled by the garbage collector
    if (array->list.capacity > ARRAY_INITIAL_CAPACITY && array->list.count / (double) array->list.capacity  < ARRAY_MIN_CAPACITY_RATIO) {
        int oldCapacity = array->list.capacity;
        array->list.capacity = SHRINK_CAPACITY(array->list.capacity);
        array->list.values = GROW_ARRAY(Value, array->list.values, oldCapacity, array->list.capacity);
    }

    return value; 
}