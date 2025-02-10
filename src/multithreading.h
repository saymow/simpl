#ifndef MULTITHREADING_H
#define MULTITHREADING_H

#include "vm.h"

ActiveThread* spawnThread(Thread* program);
ActiveThread* getThread(uint32_t threadId);
void killThread(Thread* program, uint32_t threadId);
void initLock(Thread* program, ObjString* lockId);
void lockSection(Thread* program, ObjString* lockId);
void unlockSection(Thread* program, ObjString* lockId);
void initSemaphore(Thread* program, ObjString* semaphoreId, int value);
void postSemaphore(Thread* program, ObjString* semaphoreId);
void waitSemaphore(Thread* program, ObjString* semaphoreId);

#endif
