#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main() {
  initVM();
  Chunk chunk;
  initChunk(&chunk);
  
  writeChunk(&chunk, OP_CONSTANT, 1);
  writeChunk(&chunk, addConstant(&chunk, 3), 1);
  writeChunk(&chunk, OP_NEGATE, 1);
  writeChunk(&chunk, OP_CONSTANT, 1);
  writeChunk(&chunk, addConstant(&chunk, 2), 1);
  writeChunk(&chunk, OP_MULTIPLY, 1);
  writeChunk(&chunk, OP_CONSTANT, 1);
  writeChunk(&chunk, addConstant(&chunk, 5), 1);
  writeChunk(&chunk, OP_ADD, 1);
  writeChunk(&chunk, OP_RETURN, 1);

  disassembleChunk(&chunk, "test chunk");
  interpret(&chunk);

  freeVM();
  freeChunk(&chunk);
  return 0;
}