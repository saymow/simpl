#ifndef compiler_h
#define compiler_h

#include "vm.h"

ObjFunction *compile(const char* source);
void markCompilerRoots();

#endif