#include <stdio.h>

#include "vm.h"
#include "common.h"
#include "debug.h"
#include "vm.h"

VM vm;

static void resetStack() { vm.stackTop = vm.stack; }

void initVM() { resetStack(); }

void freeVM() {}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT(constantIdx) (vm.chunk->constants.values[READ_BYTE()])

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("        ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printfValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        printfValue(constant);
        printf("\n");
        break;
      }
      case OP_NEGATE: {
        push(-pop());
        break;
      }
      case OP_RETURN: {
        printfValue(pop());
        printf("\n");
        return INTERPRET_OK;
      }

      default:
        break;
    }
  }

#undef READ_CONSTANT
#undef READ_BYTE
}

InterpretResult interpret(Chunk* chunk) {
  vm.chunk = chunk;
  vm.ip = vm.chunk->code;
  return run();
}
