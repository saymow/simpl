#ifndef vm_h
#define vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef enum { FRAME_TYPE_NORMAL, FRAME_TYPE_MODULE } CallFrameType;

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
  CallFrameType type;
  Table moduleExports;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int framesCount;

  Value stack[STACK_MAX];
  Value* stackTop;

  Table strings;
  Table global;

  ObjUpValue* upvalues;
  Obj* objects;

  int grayCount;
  int grayCapacity;
  Obj** grayStack;
  size_t bytesAllocated;
  size_t GCThreshold;

  ObjClass* moduleExportsClass;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif