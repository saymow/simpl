#ifndef compiler_h
#define compiler_h

#include "vm.h"

typedef enum {
  TYPE_SCRIPT,
  TYPE_MODULE,
  TYPE_FUNCTION,
  TYPE_LAMBDA_FUNCTION,
  TYPE_CONSTRUCTOR,
  TYPE_METHOD
} FunctionType;

ObjFunction* compile(const char* source, char* absPath);
void markCompilerRoots();

#endif