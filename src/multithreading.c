#include "multithreading.h"

#include "memory.h"
#include "pthread.h"
#include "semaphore.h"
#include "vm.h"

ActiveThread* spawnThread(Thread* program) {
  // Lock memory allocation area
  pthread_mutex_lock(&vm.memoryAllocationMutex);

  ActiveThread* activeThread = ALLOCATE(ActiveThread, 1);
  Thread* workerThread = ALLOCATE(Thread, 1);

  initProgram(workerThread);
  tableAddAll(&program->frame->namespace, &workerThread->global);

  activeThread->id = vm.threadsCount++;
  activeThread->program = workerThread;

  activeThread->next = vm.threads;
  vm.threads = activeThread;

  // Unlock memory allocation area
  pthread_mutex_unlock(&vm.memoryAllocationMutex);
  return activeThread;
}

ActiveThread* getThread(uint32_t threadId) {
  // Lock memory allocation area
  pthread_mutex_lock(&vm.memoryAllocationMutex);
  ActiveThread* thread = vm.threads;

  while (thread != NULL && thread->id != threadId) {
    thread = thread->next;
  }

  // Unlock memory allocation area
  pthread_mutex_unlock(&vm.memoryAllocationMutex);
  return thread;
}

void killThread(uint32_t threadId) {
  // Lock memory allocation area
  pthread_mutex_lock(&vm.memoryAllocationMutex);

  ActiveThread* tmp = vm.threads;
  ActiveThread* prev = NULL;

  while (tmp != NULL && tmp->id != threadId) {
    prev = tmp;
    tmp = tmp->next;
  }

  if (tmp == NULL) return;

  if (prev == NULL) {
    vm.threads = vm.threads->next;
  } else {
    prev->next = tmp->next;
  }

  freeProgram(tmp->program);
  FREE(Thread, tmp->program);
  FREE(ActiveThread, tmp);

  // Unlock memory allocation area
  pthread_mutex_unlock(&vm.memoryAllocationMutex);
}

void initLock(Thread* program, ObjString* lockId) {
  ThreadLock* tmp = vm.locks;

  while (tmp != NULL && tmp->id != lockId) {
    tmp = tmp->next;
  }

  if (tmp != NULL) {
    return recoverableRuntimeError(program, "Lock %s is already defined.",
                                   lockId->chars);
  }

  pthread_mutex_lock(&vm.memoryAllocationMutex);
  tmp = ALLOCATE(ThreadLock, 1);

  tmp->id = lockId;
  pthread_mutex_init(&tmp->mutex, NULL);

  tmp->next = vm.locks;
  vm.locks = tmp;
  pthread_mutex_unlock(&vm.memoryAllocationMutex);
}

void lockSection(Thread* program, ObjString* lockId) {
  ThreadLock* tmp = vm.locks;

  while (tmp != NULL && tmp->id != lockId) {
    tmp = tmp->next;
  }

  if (tmp == NULL) {
    return recoverableRuntimeError(
        program, "Unable to unlock undefined %s lock", lockId->chars);
  }

  pthread_mutex_lock(&tmp->mutex);
}

void unlockSection(Thread* program, ObjString* lockId) {
  ThreadLock* tmp = vm.locks;

  while (tmp != NULL && tmp->id != lockId) {
    tmp = tmp->next;
  }

  if (tmp == NULL) {
    return recoverableRuntimeError(
        program, "Unable to unlock undefined %s lock", lockId->chars);
  }

  pthread_mutex_unlock(&tmp->mutex);
}

void initSemaphore(Thread* program, ObjString* semaphoreId, int value) {
  ThreadSemaphore* tmp = vm.semaphores;

  while (tmp != NULL && tmp->id != semaphoreId) {
    tmp = tmp->next;
  }

  if (tmp != NULL) {
    recoverableRuntimeError(program, "Semaphore %s is already initialized.",
                            semaphoreId->chars);
  }

  tmp = ALLOCATE(ThreadSemaphore, 1);

  tmp->id = semaphoreId;
  sem_init(&tmp->semaphore, 0, value);

  tmp->next = vm.semaphores;
  vm.semaphores = tmp;
}

void postSemaphore(Thread* program, ObjString* semaphoreId) {
  ThreadSemaphore* tmp = vm.semaphores;

  while (tmp != NULL && tmp->id != semaphoreId) {
    tmp = tmp->next;
  }

  if (tmp == NULL) {
    recoverableRuntimeError(program, "Semaphore %s not found.",
                            semaphoreId->chars);
  }

  sem_post(&tmp->semaphore);
}

void waitSemaphore(Thread* program, ObjString* semaphoreId) {
  ThreadSemaphore* tmp = vm.semaphores;

  while (tmp != NULL && tmp->id != semaphoreId) {
    tmp = tmp->next;
  }

  if (tmp == NULL) {
    recoverableRuntimeError(program, "Semaphore %s not found.",
                            semaphoreId->chars);
  }

  sem_wait(&tmp->semaphore);
}
