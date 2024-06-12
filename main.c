#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main() {
  initVM();
  Chunk chunk;
  initChunk(&chunk);
  
  int constantIdx = addConstant(&chunk, 777);
  
  writeChunk(&chunk, OP_CONSTANT, 1);
  writeChunk(&chunk, constantIdx, 1);
  writeChunk(&chunk, OP_NEGATE, 1);
  writeChunk(&chunk, OP_RETURN, 1);

  disassembleChunk(&chunk, "test chunk");
  interpret(&chunk);

  freeVM();
  freeChunk(&chunk);
  return 0;
}