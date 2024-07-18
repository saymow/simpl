#ifndef vm_h
#define vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)
#define TRY_CATCH_STACK_MAX 8
#define LOOP_STACK_MAX 8

typedef enum { FRAME_TYPE_CLOSURE, FRAME_TYPE_MODULE } CallFrameType;

typedef enum { INITIALIZING, EXTENDING, INITIALIZED  } VMState; 


// CallFrames are either function calls or modules, both have their own 
// intruction pointer (ip) and share of the stack (slots). the namespace
// table changes a little based on the frame type. 
//
//    FRAME_TYPE_MODULE: namespace table refers to the module namespace. 
// It is: 
//       vm global namespace + 
//       user defined global variables.
//
//    FRAME_TYPE_CLOSURE: namespace table refers to the enclosing module
// namespace.   
typedef struct {
  uint8_t* ip;
  Value* slots;
  CallFrameType type;
  Table namespace;
  union {
    ObjClosure* closure;
    ObjModule* module;
  } as;
} CallFrame;

typedef struct TryCatch {
  CallFrame* frame;
  Value* frameStackTop;
  uint8_t* startIp;
  uint8_t* catchIp;
  uint8_t* outIp;
  bool hasCatchParameter;
} TryCatch;

typedef struct Loop {
  CallFrame* frame;
  Value* frameStackTop;
  uint8_t* startIp;
  uint8_t* outIp;
} Loop;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int framesCount;

  Value stack[STACK_MAX];
  Value* stackTop;

  Table strings;
  Table global;

  ObjUpValue* upvalues;
  
  // Root class, everything inherits from it
  ObjClass* klass;
  
  // Meta Classes are used to define static methods and are exposed to the global namespace
  // These are superclasses of the Data Type Classes
  ObjClass* metaArrayClass;
  ObjClass* metaStringClass;
  ObjClass* metaSystemClass;

  // Data Type Classes are superclasses of all data types
  ObjClass* nilClass;
  ObjClass* boolClass;
  ObjClass* numberClass;
  ObjClass* stringClass;
  ObjClass* functionClass;
  ObjClass* nativeFunctionClass;
  ObjClass* arrayClass;
  ObjClass* moduleExportsClass;
  ObjClass* systemClass;

  Loop loopStack[LOOP_STACK_MAX];
  int loopStackCount;
  TryCatch tryCatchStack[TRY_CATCH_STACK_MAX];
  int tryCatchStackCount; 

  // Garbage Collector fields
  // Memory allocating are handling in three ways:
  //    1. Manual allocation, e.g, module resolution
  //    2. Garbage Collector allocation, we leave the task of freeing unused Objects to the GC.
  //    3. Compound allocation, a garbage-collector-tracked object has pointers to several
  //    manually allocated objects. Garbage Collector can handle this by freing the object
  //    and its pointers, if the object has ownership of it. 

  // All tracked objects that Gargage Collector has control over
  Obj* objects;
  // Most Objects only exists in the presence of others Objects. For instance a Function
  // MUST be associated with a String in order to have a name to be referenced.
  // This field is intended to track the objects assembly line, i.e, objects that are not
  // part of the program yet (The GC would not be able to mark them otherwise), but should not be collected
  // if the garbage collection is triggered.
  // If this is not NULL, all objects up until the one it is pointing to are considered 
  // part of the assembly line and, therefore, should be marked. 
  Obj* objectsAssemblyLineEnd;

  // Quantity of bytes allocated by the program, it can be manual allocation or not.
  // TODO: files read are not being counted on this.  
  size_t bytesAllocated;
  // Threshold of bytes allocated to trigger Garbage Collector
  // TODO: there may exist a corner case in which a Garbage collection is triggered
  // because of too much manual memory allocation - which it should not.
  size_t GCThreshold;
  
  // Garbage Collector Auxiliary list of Objects it was able to mark
  // The collected objects are: ALL_OBJECTS - MARKED_OBJECTS
  // That is, objects it could not mark
  int grayCount;
  int grayCapacity;
  Obj** grayStack;

  // The process of initializing the VM is complex and need VM itself to interpret
  // some core functionalities. For this, we have a few states to tweak the VM behavior
  // a little:
  //
  //  INITIALIZING: Where the heap memory allocation, system classes, native functions 
  // and settings are established.
  //  
  //  EXTENDING: Where Simpl code is interpreted to extend core functionalities.
  //
  //  INTIALIZED: Where it is ready to interpret user code.
  //  
  VMState state;
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
InterpretResult interpret(const char* source, char* absPath);
void push(Value value);
Value pop();
Value peek(int distance);
void beginAssemblyLine(Obj* obj);
void endAssemblyLine();

#endif