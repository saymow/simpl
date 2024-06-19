#include "memory.h"

#include <stdlib.h>

#include "compiler.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>

#include "debug.h"
#endif

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    startGarbageCollector();
#endif
  }

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);

  if (result == NULL) exit(1);

  return result;
}

static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjString, object);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE(ObjClosure, closure);
      FREE_ARRAY(ObjUpValue*, closure->upvalues, closure->upvalueCount);
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
}

void markObject(Obj* obj) {
  if (obj == NULL) return;
  if (obj->isMarked) return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)obj);
  printfValue(OBJ_VAL(obj));
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

void markArray(ValueArray valueArray) {
  for (int idx = 0; idx < valueArray.count; idx++) {
    markValue(valueArray.values[idx]);
  }
}

static void markRoots() {
  for (Value* slot = vm.stackTop; slot < vm.stack; slot++) {
    markValue(*slot);
  }

  for (int idx = 0; idx < vm.framesCount; idx++) {
    markObject((Obj*)vm.frames[idx].closure);
  }

  for (ObjUpValue* upvalue = vm.upvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }

  markTable(&vm.global);
  markCompilerRoots();
}

static void blackenObject(Obj* obj) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)obj);
  printfValue(OBJ_VAL(obj));
  printf("\n");
#endif

  switch (obj->type) {
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)obj;
      markObject((Obj*)function->name);
      markArray(function->chunk.constants);
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)obj;
      for (int idx = 0; idx < closure->upvalueCount; idx++) {
        markObject((Obj*)closure->upvalues[idx]);
      }
    }
    case OBJ_UPVALUE: {
      ObjUpValue* upvalue = (ObjUpValue*)obj;
      markValue(upvalue->closed);
    }
    case OBJ_STRING:
    case OBJ_NATIVE_FN:
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

void startGarbageCollector() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
#endif

  markRoots();
  crawlReferences();
  tableRemoveNotReferenced(&vm.strings);
  sweep();

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
#endif
};