#include "memory.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>

#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  // Lock memory allocation area
  pthread_mutex_lock(&vm.memoryAllocationMutex);

  vm.bytesAllocated += newSize - oldSize;

  if (newSize > oldSize && vm.state == INITIALIZED && !vm.GCTriggered) {
#ifdef DEBUG_STRESS_GC
    triggerGarbageCollector();
#else
    if (vm.bytesAllocated >= vm.GCThreshold) {
      triggerGarbageCollector();
    }
#endif
  }

  if (newSize == 0) {
    free(pointer);

    // Unlock memory allocation area
    pthread_mutex_unlock(&vm.memoryAllocationMutex);
    return NULL;
  }

  void* result = realloc(pointer, newSize);

  if (result == NULL) exit(1);

  // Unlock memory allocation area
  pthread_mutex_unlock(&vm.memoryAllocationMutex);
  return result;
}

static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case OBJ_BOUND_OVERLOADED_METHOD: {
      ObjBoundOverloadedMethod* boundOverloadedMethod =
          (ObjBoundOverloadedMethod*)object;
      FREE(ObjBoundOverloadedMethod, boundOverloadedMethod);
      break;
    }
    case OBJ_OVERLOADED_METHOD: {
      ObjOverloadedMethod* overloadedMethod = (ObjOverloadedMethod*)object;
      FREE(ObjBoundOverloadedMethod, overloadedMethod);
      break;
    }
    case OBJ_ARRAY: {
      ObjArray* array = (ObjArray*)object;
      freeValueArray(&array->list);
      FREE(ObjArray, array);
      break;
    }
    case OBJ_MODULE: {
      ObjModule* module = (ObjModule*)object;
      markValue(module->exports);
      FREE(ObjModule, module);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      freeTable(&instance->properties);
      FREE(ObjInstance, instance);
      break;
    }
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      freeTable(&klass->methods);
      FREE(ObjClass, klass);
      break;
    }
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpValue*, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, closure);
      break;
    }
    case OBJ_NATIVE_FN: {
      FREE(ObjNativeFn, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(ObjUpValue, object);
      break;
    }
  }
}

void freeObjects() {
  Obj* object = vm.objects;

  while (object != NULL) {
    Obj* tmp = object->next;
    freeObject(object);
    object = tmp;
  }

  free(vm.grayStack);
}

void markObject(Obj* obj) {
  if (obj == NULL) return;
  if (obj->isMarked) return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)obj);
  printValue(OBJ_VAL(obj));
  printf("\n");
#endif

  obj->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
  }

  if (vm.grayStack == NULL) exit(1);

  vm.grayStack[vm.grayCount++] = obj;
}

void markValue(Value value) {
  if (!IS_OBJ(value)) return;
  markObject(AS_OBJ(value));
}

void markArray(ValueArray* valueArray) {
  for (int idx = 0; idx < valueArray->count; idx++) {
    markValue(valueArray->values[idx]);
  }
}

static void markGCWhiteList() {
  for (int idx = 0; idx < vm.GCWhiteListCount; idx++) {
    markObject((Obj*)vm.GCWhiteList[idx]);
  }
}

static void markProgram(Thread* program) {
  markTable(&program->global);

  for (Value* slot = program->stack; slot < program->stackTop; slot++) {
    markValue(*slot);
  }

  for (int idx = 0; idx < program->framesCount; idx++) {
    markTable(&program->frames[idx].namespace);
    if (program->frames[idx].type == FRAME_TYPE_MODULE) {
      markObject((Obj*)program->frames[idx].as.module);
    } else {
      markObject((Obj*)program->frames[idx].as.closure);
    }
  }

  for (ObjUpValue* upvalue = program->upvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }
}

static void markThreads() {
  ActiveThread* thread = vm.threads;

  while (thread != NULL) {
    markProgram(thread->program);
    thread = thread->next;
  }
}

static void markRoots() {
  markTable(&vm.modules);
  markProgram(&vm.program);
  markThreads();
  markGCWhiteList();
  markCompilerRoots();
  markObject((Obj*)vm.lambdaFunctionName);
  markObject((Obj*)vm.klass);
  markObject((Obj*)vm.metaArrayClass);
  markObject((Obj*)vm.metaStringClass);
  markObject((Obj*)vm.metaNumberClass);
  markObject((Obj*)vm.metaMathClass);
  markObject((Obj*)vm.metaErrorClass);
  markObject((Obj*)vm.metaSystemClass);
  markObject((Obj*)vm.metaObjectClass);
  markObject((Obj*)vm.nilClass);
  markObject((Obj*)vm.boolClass);
  markObject((Obj*)vm.numberClass);
  markObject((Obj*)vm.mathClass);
  markObject((Obj*)vm.stringClass);
  markObject((Obj*)vm.functionClass);
  markObject((Obj*)vm.nativeFunctionClass);
  markObject((Obj*)vm.arrayClass);
  markObject((Obj*)vm.errorClass);
  markObject((Obj*)vm.moduleExportsClass);
  markObject((Obj*)vm.systemClass);
  markObject((Obj*)vm.objectClass);
}

static void blackenObject(Obj* obj) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)obj);
  printValue(OBJ_VAL(obj));
  printf("\n");
#endif

  markObject((Obj*) obj->klass);

  switch (obj->type) {
    case OBJ_BOUND_OVERLOADED_METHOD: {
      ObjBoundOverloadedMethod* boundOverloadedMethod =
          (ObjBoundOverloadedMethod*)obj;
      markValue(boundOverloadedMethod->base);
      markObject((Obj*)boundOverloadedMethod->overloadedMethod);
      break;
    }
    case OBJ_OVERLOADED_METHOD: {
      ObjOverloadedMethod* overloadedMethod = (ObjOverloadedMethod*)obj;
      markObject((Obj*)overloadedMethod->name);
      if (overloadedMethod->type == USER_METHOD) {
        for (int idx = 0; idx < ARGS_ARITY_MAX; idx++) {
          markObject((Obj*)overloadedMethod->as.userMethods[idx]);
        }
      } else {
        for (int idx = 0; idx < ARGS_ARITY_MAX; idx++) {
          markObject((Obj*)overloadedMethod->as.nativeMethods[idx]);
        }
      }
      break;
    }
    case OBJ_ARRAY: {
      ObjArray* array = (ObjArray*)obj;
      markArray(&array->list);
      break;
    }
    case OBJ_MODULE: {
      ObjModule* module = (ObjModule*)obj;
      markValue(module->exports);
      markObject((Obj*)module->function);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)obj;
      markObject((Obj*)instance->obj.klass);
      markTable(&instance->properties);
      break;
    }
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)obj;
      markObject((Obj*)klass->name);
      markTable(&klass->methods);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)obj;
      markObject((Obj*)function->name);
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)obj;
      markObject((Obj*)closure->function);
      for (int idx = 0; idx < closure->upvalueCount; idx++) {
        markObject((Obj*)closure->upvalues[idx]);
      }
      break;
    }
    case OBJ_UPVALUE: {
      ObjUpValue* upvalue = (ObjUpValue*)obj;
      markValue(upvalue->closed);
      break;
    }
    case OBJ_NATIVE_FN: {
      markObject((Obj*)((ObjNativeFn*)obj)->name);
      break;
    }
    case OBJ_STRING:
      break;
  }
}

static void crawlReferences() {
  while (vm.grayCount > 0) {
    blackenObject(vm.grayStack[--vm.grayCount]);
  }
}

static void sweep() {
  Obj* prev = NULL;
  Obj* current = vm.objects;

  while (current != NULL) {
    if (current->isMarked) {
      current->isMarked = false;
      prev = current;
      current = current->next;
    } else {
      Obj* unmarked = current;

      if (prev != NULL) {
        prev->next = current->next;
      } else {
        vm.objects = current->next;
      }

      current = current->next;

      freeObject(unmarked);
    }
  }
}

void triggerGarbageCollector() {
  pthread_t threadId;
  if (pthread_create(&threadId, NULL, startGarbageCollector, NULL) != 0) {
    fprintf(stderr, "Can't spawn garbage collector thread\n");
    exit(1);
  }

  vm.GCTriggered = true;
#if defined(DEBUG_LOG_GC) || defined(DEBUG_LOG_GC_SAFEZONE)
  printf("-- gc collector is triggered\n");
#endif
}

void* startGarbageCollector() {
  pthread_mutex_lock(&vm.GCMutex);
  vm.GCThreadSpawned = true;
#if defined(DEBUG_LOG_GC) || defined(DEBUG_LOG_GC_SAFEZONE)
  printf("-- gc collector thread is spawned (%d/%d program threads synchronized)\n", 
        vm.safezoneCounter, vm.threadsCounter);
#endif

#if defined(DEBUG_LOG_GC) || defined(DEBUG_LOG_GC_SAFEZONE)
  uint32_t count = 0;
#endif

  while (vm.safezoneCounter < vm.threadsCounter) {
#if defined(DEBUG_LOG_GC) || defined(DEBUG_LOG_GC_SAFEZONE)
    if (count % 10000000 == 0) {
      printf("-- gc collector is awating for sync (%d/%d program threads synchronized)\n", 
          vm.safezoneCounter, vm.threadsCounter);
    }
    count++;
#endif
    pthread_mutex_unlock(&vm.GCMutex);
    pthread_mutex_lock(&vm.GCMutex);
  }

#if defined(DEBUG_LOG_GC) || defined(DEBUG_LOG_GC_SAFEZONE)
  int before = vm.bytesAllocated;
  printf("-- gc collector thread is synchronized (%d/%d program threads synchronized)\n", 
      vm.safezoneCounter, vm.threadsCounter);
  printf("-- gc collector thread begin\n");
#endif

  markRoots();
  crawlReferences();
  tableRemoveNotReferenced(&vm.strings);
  sweep();

  vm.GCThreshold = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
  vm.GCThreadSpawned = false;
  vm.GCTriggered = false;
  
#if defined(DEBUG_LOG_GC) || defined(DEBUG_LOG_GC_SAFEZONE)
  printf("-- gc collector thread end\n");
  printf(" collected %ld bytes (from %d to %ld) next at %ld\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.GCThreshold);
  printf(" program threads about to be released\n");
#endif

  pthread_mutex_unlock(&vm.GCMutex);
  pthread_cond_broadcast(&vm.GCSafezoneCond);

  return NULL;
}

void enterGCSafezone(Thread* thread) {
  pthread_mutex_lock(&vm.GCMutex);

  vm.safezoneCounter++;
  #ifdef DEBUG_LOG_GC_SAFEZONE
    printf("-- gc program %d thread entered safe zone (%d/%d program threads synchronized)\n",
            thread->id, vm.safezoneCounter, vm.threadsCounter);
  #endif

  pthread_mutex_unlock(&vm.GCMutex);
}

void leaveGCSafezone(Thread* thread) {
  pthread_mutex_lock(&vm.GCMutex);
  
  while (vm.GCThreadSpawned) {
    #ifdef DEBUG_LOG_GC_SAFEZONE
      printf("-- gc program %d thread is about to sleep (%d/%d program threads synchronized)\n",
              thread->id, vm.safezoneCounter, vm.threadsCounter);
    #endif
    pthread_cond_wait(&vm.GCSafezoneCond, &vm.GCMutex);
    #ifdef DEBUG_LOG_GC_SAFEZONE
      printf("-- gc program %d thread waked up (%d/%d program threads synchronized)\n",
              thread->id, vm.safezoneCounter, vm.threadsCounter);
    #endif
  }

  vm.safezoneCounter--;
  #ifdef DEBUG_LOG_GC_SAFEZONE
    printf("-- gc program %d thread left safe zone (%d/%d program threads synchronized)\n", 
            thread->id, vm.safezoneCounter, vm.threadsCounter);
  #endif
  pthread_mutex_unlock(&vm.GCMutex);
}