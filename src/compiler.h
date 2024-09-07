#ifndef compiler_h
#define compiler_h

#include "vm.h"

// Helper Compiler type enum
typedef enum {
  // Main function. Think of as if the program is wrapped in the main function.
  TYPE_SCRIPT,
  
  // Module "main function". 
  TYPE_MODULE,
  
  // Named function 
  TYPE_FUNCTION,
  
  // Nameless, anonymous function or function expression 
  TYPE_LAMBDA_FUNCTION,

  // Class constructor
  TYPE_CONSTRUCTOR,

  // Class method
  TYPE_METHOD
} FunctionType;

// Compile source code
ObjFunction* compile(const char* source, char* absPath);

// Mark all compiler garbage-collector-tracked objects
void markCompilerRoots();

#endif