#ifndef utils_h
#define utils_h

#include "common.h"

char *readFile(const char *path);
char *getFileAbsPath(const char *relativePath);
char *resolvePath(const char *entryFilePath, const char *filePath,
                  const char *relativePath);
uint32_t hashString(const char *key, int length);

#endif