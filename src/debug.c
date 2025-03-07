#include "debug.h"

#include "chunk.h"
#include "object.h"
#include "stdio.h"

void disassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

static int flaggedSimpleInstruction(const char* name, Chunk* chunk,
                                    int offset) {
  uint8_t flag = chunk->code[offset + 1];
  printf("%s | %d\n", name, flag);
  return offset + 2;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2;
}

static int invokeInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  uint8_t argCount = chunk->code[offset + 2];
  printf("%-16s (%d args) %4d '", name, argCount, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 3;
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk,
                           int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

static int loopGuardInstruction(const char* name, Chunk* chunk, int offset) {
  uint16_t loopStartJump = (uint16_t)(chunk->code[offset + 1] << 8);
  loopStartJump |= chunk->code[offset + 2];
  uint16_t loopEndJump = (uint16_t)(chunk->code[offset + 3] << 8);
  loopEndJump |= chunk->code[offset + 4];
  printf("%-16s %4d ->  %d | %d \n", name, offset, offset + loopStartJump + 5,
         offset + loopEndJump + 5);

  return offset + 5;
}

static int tryCatchInstruction(const char* name, Chunk* chunk, int offset) {
  uint16_t catchJump = (uint16_t)(chunk->code[offset + 1] << 8);
  catchJump |= chunk->code[offset + 2];
  uint16_t outJump = (uint16_t)(chunk->code[offset + 3] << 8);
  outJump |= chunk->code[offset + 4];
  bool hasCatchParameter = chunk->code[offset + 5];
  printf("%-16s %4d ->  %d | %d | %d \n", name, offset, offset + catchJump + 6,
         offset + outJump + 6, hasCatchParameter);

  return offset + 6;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constantIdx = chunk->code[offset + 1];
  printf("%-16s %4d '", name, constantIdx);
  printValue(chunk->constants.values[constantIdx]);
  printf("'\n");
  return offset + 2;
}

static int flaggedConstantInstruction(const char* name, Chunk* chunk,
                                      int offset) {
  uint8_t constantIdx = chunk->code[offset + 1];
  uint8_t flag = chunk->code[offset + 2];
  printf("%-16s %4d '", name, constantIdx);
  printValue(chunk->constants.values[constantIdx]);
  printf(" | %d'\n", flag);
  return offset + 3;
}

int disassembleInstruction(Chunk* chunk, int offset) {
  uint8_t instruction = chunk->code[offset];

  printf("%04d ", offset);

  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }

  switch (instruction) {
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_STRING_INTERPOLATION:
      return constantInstruction("OP_STRING_INTERPOLATION", chunk, offset);
    case OP_CLASS:
      return constantInstruction("OP_CLASS", chunk, offset);
    case OP_INHERIT:
      return simpleInstruction("OP_INHERIT", offset);
    case OP_SUPER:
      return constantInstruction("OP_SUPER", chunk, offset);
    case OP_GET_PROPERTY:
      return flaggedConstantInstruction("OP_GET_PROPERTY", chunk, offset);
    case OP_SET_PROPERTY:
      return constantInstruction("OP_SET_PROPERTY", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OP_METHOD:
      return constantInstruction("OP_METHOD", chunk, offset);
    case OP_GET_LOCAL:
      return byteInstruction("OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL:
      return byteInstruction("OP_SET_LOCAL", chunk, offset);
    case OP_GET_UPVALUE:
      return byteInstruction("OP_GET_UPVALUE", chunk, offset);
    case OP_SET_UPVALUE:
      return byteInstruction("OP_SET_UPVALUE", chunk, offset);
    case OP_CALL:
      return byteInstruction("OP_CALL", chunk, offset);
    case OP_ARRAY:
      return byteInstruction("OP_ARRAY", chunk, offset);
    case OP_INVOKE:
      return invokeInstruction("OP_INVOKE", chunk, offset);
    case OP_CLOSURE: {
      offset++;
      uint8_t constant = chunk->code[offset++];
      printf("%-16s %4d", "OP_CLOSURE", constant);
      printValue(chunk->constants.values[constant]);
      printf("\n");

      ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
      for (int idx = 0; idx < function->upvalueCount; idx++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf("%04d    | %s %d\n", offset - 2, isLocal ? "local" : "upvalue",
               index);
      }
      return offset;
    }
    case OP_GET_ITEM:
      return flaggedSimpleInstruction("OP_GET_ITEM", chunk, offset);
    case OP_SET_ITEM:
      return simpleInstruction("OP_SET_ITEM", offset);
    case OP_EXPORT:
      return simpleInstruction("OP_EXPORT", offset);
    case OP_POP:
      return simpleInstruction("OP_POP", offset);
    case OP_CLOSE_UPVALUE:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_ADD:
      return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:
      return simpleInstruction("OP_DIVIDE", offset);
    case OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);
    case OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction("OP_GREATER", offset);
    case OP_LESS:
      return simpleInstruction("OP_LESS", offset);
    case OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case OP_LOOP_GUARD_END:
      return simpleInstruction("OP_LOOP_GUARD_END", offset);
    case OP_LOOP_BREAK:
      return simpleInstruction("OP_LOOP_BREAK", offset);
    case OP_LOOP_CONTINUE:
      return simpleInstruction("OP_LOOP_CONTINUE", offset);
    case OP_SWITCH_BREAK:
      return simpleInstruction("OP_SWITCH_BREAK", offset);
    case OP_SWITCH_END:
      return jumpInstruction("OP_SWITCH_END", -1, chunk, offset);
    case OP_JUMP:
      return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_SWITCH:
      return jumpInstruction("OP_SWITCH", 1, chunk, offset);
    case OP_SWITCH_CASE:
      return jumpInstruction("OP_SWITCH_CASE", 1, chunk, offset);
    case OP_RANGED_LOOP_SETUP:
      return simpleInstruction("OP_RANGED_LOOP_SETUP", offset);
    case OP_RANGED_LOOP:
      return simpleInstruction("OP_RANGED_LOOP", offset);
    case OP_NAMED_LOOP:
      return simpleInstruction("OP_NAMED_LOOP", offset);
    case OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, chunk, offset);
    case OP_LOOP_GUARD:
      return loopGuardInstruction("OP_LOOP_GUARD", chunk, offset);
    case OP_TRY_CATCH:
      return tryCatchInstruction("OP_TRY_CATCH", chunk, offset);
    case OP_TRY_CATCH_TRY_END:
      return simpleInstruction("OP_TRY_CATCH_TRY_END", offset);
    case OP_THROW:
      return simpleInstruction("OP_THROW", offset);
    case OP_IMPORT:
      return byteInstruction("OP_IMPORT", chunk, offset);
    case OP_OBJECT:
      return byteInstruction("OP_OBJECT", chunk, offset);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}