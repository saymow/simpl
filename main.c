#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "debug.h"

int main() {
  Chunk chunk;
  initChunk(&chunk);
  
  int constantIdx = addConstant(&chunk, 777);
  
  writeChunk(&chunk, OP_CONSTANT, 1);
  writeChunk(&chunk, constantIdx, 1);
  writeChunk(&chunk, OP_RETURN, 1);

  disassembleChunk(&chunk, "test chunk");
  
  freeChunk(&chunk);
  return 0;
}