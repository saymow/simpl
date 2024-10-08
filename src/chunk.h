#ifndef chunk_h
#define chunk_h

#include "common.h"
#include "value.h"

typedef enum {
  OP_POP,
  OP_CONSTANT,
  OP_STRING_INTERPOLATION,
  OP_ARRAY,
  OP_DEFINE_GLOBAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_ITEM,
  OP_SET_ITEM,
  OP_INVOKE,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
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
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP_GUARD,
  OP_LOOP_BREAK,
  OP_LOOP_CONTINUE,
  OP_LOOP_GUARD_END,
  OP_NAMED_LOOP,
  OP_RANGED_LOOP_SETUP,
  OP_RANGED_LOOP,
  OP_LOOP,
  OP_CALL,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_CLASS,
  OP_SUPER,
  OP_INHERIT,
  OP_METHOD,
  OP_EXPORT,
  OP_IMPORT,
  OP_TRY_CATCH,
  OP_TRY_CATCH_TRY_END,
  OP_THROW,
  OP_OBJECT,
  OP_SWITCH,
  OP_SWITCH_BREAK,
  OP_SWITCH_DEFAULT,
  OP_SWITCH_END,
  OP_SWITCH_CASE,
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