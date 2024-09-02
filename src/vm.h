#ifndef vm_h
#define vm_h

#include <pthread.h>
#include <semaphore.h>

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)
#define GC_WHITE_LIST_MAX 16

#define TRY_CATCH_STACK_MAX 8
#define LOOP_STACK_MAX 8
#define SWITCH_STACK_MAX 8
#define BLOCK_MAX LOOP_STACK_MAX + SWITCH_STACK_MAX 

typedef enum { FRAME_TYPE_CLOSURE, FRAME_TYPE_MODULE } CallFrameType;

typedef enum { INITIALIZING, EXTENDING, INITIALIZED  } VMState; 


// CallFrames are either function calls or modules, both have their own 
// instruction pointer (ip) and share of the stack (slots). the namespace
// table changes a little based on the frame type. 
//
//    FRAME_TYPE_MODULE: namespace table refers to the module namespace. 
// It is: 
//       vm global namespace + 
//       user defined global variables.
//
//    FRAME_TYPE_CLOSURE: namespace table refers to the enclosing module
// namespace.
//   
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

typedef struct Switch {
  CallFrame* frame;
  Value* expression;
  uint8_t* startIp;
  uint8_t* outIp;
  // bool to indicate if a "case" or "default" criteria is met
  bool fallThrough; 
} Switch;

typedef struct {
  // Program frames 
  CallFrame frames[FRAMES_MAX];
  int framesCount;
  
  // Current frame pointer
  CallFrame* frame;

  // Base namespace for all modules, where native classes are defined   
  Table global;

  // Program stack
  Value stack[STACK_MAX];
  // Program stack pointer
  Value* stackTop;
  
  // Closure is handled through upvalues.
  // This list stores open upvalues, i.e, access to variables in an outer scope
  // other than the global.
  ObjUpValue* upvalues;

  // Active Loop registers
  Loop loopStack[LOOP_STACK_MAX];
  int loopStackCount;

  // Active try-catch block registers
  TryCatch tryCatchStack[TRY_CATCH_STACK_MAX];
  int tryCatchStackCount; 

  // Active switch block registers
  Switch switchStack[SWITCH_STACK_MAX];
  int switchStackCount;
} Thread;

typedef struct ActiveThread {
  // id
  uint32_t id;
  // pthreads id
  pthread_t pthreadId; 
  // Program thread
  Thread* program;
  // Pointer to next active thread
  struct ActiveThread* next;
} ActiveThread;

typedef struct ThreadLock {
  // id
  ObjString* id;
  // pthreads mutex
  pthread_mutex_t mutex;
  // Pointer to next active thread
  struct ThreadLock* next; 
} ThreadLock;

typedef struct ThreadSemaphore {
  // id
  ObjString* id;
  // pthreads mutex
  sem_t semaphore;
  // Pointer to next active thread
  struct ThreadSemaphore* next; 
} ThreadSemaphore;

typedef struct {
  // String interning table.
  // For performance sake, strings are interned and reused in case it appears
  // somewhere else in the code.
  Table strings;

  // Process main thread program
  Thread program;

  // Process active threads linked list 
  ActiveThread* threads;
  // Used to assign threads ids and keep them trackable 
  uint32_t threadsCount;

  // Process critical sections locks linked list 
  ThreadLock* locks;
  // Process semaphores linked list 
  ThreadSemaphore* semaphores;
  
  // Root class, everything inherits from it
  ObjClass* klass;
  // Meta Classes are used to define static methods and are exposed to the global namespace
  // These are superclasses of the Data Type Classes
  // - Where Array static methods are defined 
  ObjClass* metaArrayClass;
  // - Where String static methods are defined
  ObjClass* metaStringClass;
  // - Where Number static methods are defined
  ObjClass* metaNumberClass;
  // - Where Math static methods are defined
  ObjClass* metaMathClass;
  // - Superclass for the Error class, where the Error constructor is
  ObjClass* metaErrorClass;
  // - Where System methods are defined
  ObjClass* metaSystemClass;
  // - Where Object static methods are defined
  ObjClass* metaObjectClass;
   // - Where System.Sync static utils methods are defined
  ObjClass* metaSystemSyncClass;

  // Data Type Classes are superclasses of all data types
  // - Where nil literal inherits from
  ObjClass* nilClass;
  // - Where bools literals inherits from
  ObjClass* boolClass;
  // - Where number literals inherits from
  ObjClass* numberClass;
  // - Placeholder Math library subclass (used to access superclass) 
  ObjClass* mathClass;
  // - Where string literals inherits from
  ObjClass* stringClass;
  // - Where user methods inherits from
  ObjClass* functionClass;
  // - Where native methods inherits from
  ObjClass* nativeFunctionClass;
  // - Where arrays inherits from
  ObjClass* arrayClass;
  // - Standard Error class
  ObjClass* errorClass;
  // - Where exports objects inherits from 
  ObjClass* moduleExportsClass;
  // - Placeholder System subclass (used to access superclass)
  ObjClass* systemClass;
  // - Placeholder System object (used to access superclass)
  // This class is not part of objectInstance inherintance chain
  ObjClass* objectClass;
  // - Placeholder System.Sync utility object (used to access superclass)
  ObjClass* syncClass;
  // Default name for lambda functions
  ObjString* lambdaFunctionName;

  // Only one thread can allocate memory at a time, in order to avoid complications with the GC
  // This mutex is used to guarantee mutual exclusion between threads.
  pthread_mutex_t memoryAllocationMutex;
  pthread_mutexattr_t memoryAllocationMutexAttr;
  
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
  Obj* GCWhiteList[GC_WHITE_LIST_MAX];
  int GCWhiteListCount;
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
  Obj** grayStack;
  int grayCount;
  int grayCapacity;
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
ActiveThread* spawnThread(Thread* program);
ActiveThread* getThread(uint32_t threadId);
void killThread(uint32_t threadId);
void lockSection(ObjString* lockId);
void unlockSection(Thread* program, ObjString* lockId);
void initSemaphore(Thread* program, ObjString* semaphoreId, int value);
void postSemaphore(Thread* program, ObjString* semaphoreId);
void waitSemaphore(Thread* program, ObjString* semaphoreId);
void freeProgram(Thread* program);
void freeVM();
InterpretResult interpret(const char* source, char* absPath);
bool callEntry(Thread* thread, ObjClosure* closure);
InterpretResult run(Thread* program);
void push(Thread* program, Value value);
Value pop(Thread* program);
Value peek(Thread* program, int distance);
ObjString* stackTrace(Thread* program);
Obj* GCWhiteList(Obj* obj);
void GCPopWhiteList();

#endif