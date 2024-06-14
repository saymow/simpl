#ifndef chunk_h
#define chunk_h

#include "common.h"
#include "value.h"

typedef enum {
  OP_POP,
  OP_CONSTANT,
  OP_TRUE,
  OP_FALSE,
  OP_NIL,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_GREATER,
  OP_LESS,
  OP_EQUAL,
  OP_NEGATE,
  OP_NOT,
  OP_PRINT,
  OP_RETURN,
} OpCode;

typedef struct {
  int count;
  int capacity;
  int* lines;
  uint8_t* code;
  ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);
void freeChunk(Chunk* chunk);

#endif