#ifndef utils_h
#define utils_h

#include "common.h"

char *readFile(const char *path);
uint32_t hashString(const char *key, int length);

#endif