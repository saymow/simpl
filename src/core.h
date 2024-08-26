#ifndef core_h
#define core_h

#include "vm.h"

void initCore(VM* vm);
void attachCore(VM* vm, Thread* thread);

#endif