#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

#if IS_WINDOWS
  #include <windows.h>
  #include <shlwapi.h>
#else
  #include <limits.h>
  #include <unistd.h>
#endif

char *readFile(const char *path) {
  FILE *file = fopen(path, "rb");

  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(fileSize + 1);

  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

char *getFileAbsPath(const char* relativePath) {
  #if IS_WINDOWS
    char* absPath = (char*) malloc(sizeof(char) * MAX_PATH);

    if (absPath == NULL) {
      fprintf(stderr, "Not enough memory to allocate absolute path for \"%s\"\n", relativePath);
      exit(74);
    }
  
    if (GetFullPathName(relativePath, MAX_PATH, absPath, NULL) == 0) {
      DWORD error = GetLastError();
      fprintf(stderr, "Error %lu: GetFullPathName failed\n", error);
      exit(1);
    }

    return absPath;
  #else
    printf("todo: implement getFileAbsPath for unix.\n");
    exit(1);
  #endif
}

char *removePathLastFragment(const char* path) {
  int pathLen = strlen(path);
  char* newPath = (char*) malloc(sizeof(char) * pathLen + 1);
  int newPathLen = -1;

  if (newPath == NULL) {
    fprintf(stderr, "Not enough memory to allocate for path.\n");
  }

  for (int idx = pathLen - 1; idx >= 0; idx--) {
    if (path[idx] == '\\') {
      newPathLen = idx + 1;
      break;
    }
  }

  strncpy(newPath, path, newPathLen);
  newPath[newPathLen] = '\0';

  return newPath;
}

char *resolvePath(const char* entryFilePath, const char* filePath, const char* importPath) {
   #if IS_WINDOWS
    char* absPath = (char*) malloc(sizeof(char) * MAX_PATH);
    char* basePath = removePathLastFragment(PathIsRelativeA(importPath) ? entryFilePath : filePath);

    if (!PathCombineA(absPath, basePath, importPath)) {
      fprintf(stderr, "Error combining paths.\n");
      exit(1);
    }

    if (GetFullPathName(absPath, MAX_PATH, absPath, NULL) == 0) {
      DWORD error = GetLastError();
      fprintf(stderr, "Error %lu: GetFullPathName failed\n", error);
      exit(1);
    }

    free(basePath);

    return absPath;
  #else
    printf("todo: implement resolvePath for unix.\n");
    exit(1);
  #endif
}

uint32_t hashString(const char *key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }
  return hash;
}