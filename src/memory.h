#ifndef memory_h
#define memory_h

#include "common.h"
#include "object.h"
#include "vm.h"

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

// Shortcut for mem allocation
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// Free object
void freeObjects();

// Mark value using Mark-and-sweep
void markValue(Value value);

// Mark object using Mark-and-sweep
void markObject(Obj* obj);

// Trigger a GC run
void triggerGarbageCollector();

// GC thread entry point
void* startGarbageCollector();

// Run a standard GC safezone 
void passGCSafezone(Thread *thread);

// Enter forced GC safe zone 
void enterGCSafezone(Thread *thread);

// Leave forced GC safe zone
void leaveGCSafezone(Thread *thread);

#endif