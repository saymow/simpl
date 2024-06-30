#ifndef vm_h
#define vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef enum { FRAME_TYPE_CLOSURE, FRAME_TYPE_MODULE } CallFrameType;

typedef struct {
  uint8_t* ip;
  Value* slots;
  CallFrameType type;
  union {
    ObjClosure* closure;
    ObjModule* module;
  } as;
} CallFrame;

typedef struct TryCatch {
  CallFrame* frame;
  Value* frameStackTop;
  uint8_t* catchIp;
  uint8_t* outIp;
  struct TryCatch* next;  
} TryCatch;

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
  ObjClass* arrayClass;

  TryCatch* tryCatch;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

#define IS_FRAME_MODULE(frame) ((frame)->type == FRAME_TYPE_MODULE)
#define IS_FRAME_CLOSURE(frame) ((frame)->type == FRAME_TYPE_CLOSURE)

#define FRAME_AS_MODULE(frame) ((frame)->as.module)
#define FRAME_AS_CLOSURE(frame) ((frame)->as.closure)

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif