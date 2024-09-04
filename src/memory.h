#ifndef memory_h
#define memory_h

#include "common.h"
#include "object.h"

#define ARRAY_INITIAL_CAPACITY 8

#define ARRAY_MIN_CAPACITY_RATIO 0.2

#define ALLOCATE(type, count) (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0);

#define GROW_CAPACITY(capacity) \
  (capacity < ARRAY_INITIAL_CAPACITY ? ARRAY_INITIAL_CAPACITY : (capacity) * 2)

#define SHRINK_CAPACITY(capacity) \
  (capacity < ARRAY_INITIAL_CAPACITY ? ARRAY_INITIAL_CAPACITY : (capacity) / 4)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
  (type*)reallocate(pointer, sizeof(type) * oldCount, sizeof(type) * newCount)

#define FREE_ARRAY(type, pointer, oldCount) \
  reallocate(pointer, sizeof(type) * oldCount, 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void freeObjects();
void markValue(Value value);
void markObject(Obj* obj);
void startGarbageCollector();

#endif