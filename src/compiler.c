#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"
#include "memory.h"
#include "modules.h"
#include "object.h"
#include "utils.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// Helper types used to run the Pratt Parser algorithm.
// https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY,
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

// Helper struct to provide information if we are compiling a class
typedef struct ClassCompiler {
  // Enclosing class compiler - in case we compile a class inside another class
  struct ClassCompiler* enclosing;

  // Class name token, helpful for compiling the "this" keyword.
  Token name;

  // Helpful for compiler the "super" keyword
  bool hasSuperclass;
} ClassCompiler;

// The parser, just as the Lexer, is updated during two situations:
//
//    1. compiling a module
//    2. compiling a string interpolation placeholder
typedef struct {
  // Parser's module
  ModuleNode* module;

  // Current token
  Token current;

  // Previous token
  Token previous;

  // Error flag set if at least one error occurred during compilation
  bool hadError;

  // Panic mode flag used to recover from errors and continue the compilation.
  // This recovery is only useful for IDE tools used for analysis.
  // https://stackoverflow.com/questions/66858030/understanding-the-heuristics-in-error-recovery-panic-mode-in-predictive-parsin
  bool panicMode;
} Parser;

// Helper struct used to handle local variables (are kept on the program stack)
typedef struct {
  // Token name
  Token name;

  // Scope depth
  int depth;

  // Flag used to ensure correct implementation of closures and outer scope
  // captured variables.
  bool isCaptured;
} Local;

// Helper struct to ensure propper closure implementation.
// More on Lua's closure implemention:
// https://poga.github.io/lua53-notes/function_closure.html
typedef struct {
  // Local variables are stored on the program stack.
  // this index is used to emit the bytecode used to access the enclosured
  // upvalue.
  uint8_t index;

  // Flag used to identify if the upvalue variable is local.
  bool isLocal;
} UpValue;

// Helper block type structure
typedef enum { SWITCH_BLOCK, LOOP_BLOCK } BlockType;
typedef struct {
  BlockType type;
} Block;

// Helper Compiler structure
typedef struct Compiler {
  // Source code file (if not in repl mode) absolute to the system path
  char* absPath;

  // Program function
  ObjFunction* function;

  // Compiler behavior may chaange based on the compiler type.
  FunctionType type;

  // Track all local variables.
  // This list mirrors the program stack and is used to get the correct index
  // for accessing local variables. UINT8_MAX is a hard limit of all constant
  // instructions. We only use one byte (256 distinct values) as indexes to map
  // local variables, e.g:
  Local locals[UINT8_MAX];
  int localCount;

  // Block depth of the code the in compilation
  int scopeDepth;

  // Track all upvalue variables.
  UpValue upvalues[UINT8_MAX];

  // Pointer the previous function in compilation.
  // Compiler structs are chained allocated.
  struct Compiler* enclosing;

  // This chain structure is not only helpful for keeping track of other
  // Compiler structs but for semanthics too. This parallel chain structure is
  // used mainly for isolating variables between modules, e.g:
  //
  //    enclosing: Main <- module_A <- module_B <- function_A <- function_B
  //    semanticallyEnclosing: Main | module_A | module_B <- function_A <- function_B
  //
  // This allows for variable-access isolation between modules.
  struct Compiler* semanticallyEnclosing;

  // This ugly list is used to keep track of whether we are in a switch or loop
  // block, which type of block is, and how nested we are.
  Block blockStack[LOOP_STACK_MAX + SWITCH_STACK_MAX];
  int blockStackCount;

  // Modules are expected to use the export statement just once.  
  bool hasExported; 
} Compiler;

ObjFunction* compile(const char* source, char* absPath);

void markCompilerRoots();

// Get compiler current function chunk
static Chunk* currentChunk();

// Init a new compiler struct
static void initCompiler(Compiler* compiler, char* absPath, FunctionType type);

// Emit chunk RETURN. All chunks are terminated like this
static void emitReturn();

// End current compiler and get back to enclosing compiler
static ObjFunction* endCompiler();

// Error at a given token
static void errorAt(Token* token, const char* message);

// Error at previous consumed token
static void error(const char* message);

// Error at current token
static void errorAtCurrent(const char* message);

// Advance to the next token
static void advance();

// Check if current token is of given type
static bool check(TokenType tokenType);

// Check if current token is of given type. If so, consume
static bool match(TokenType type);

// Consume current token
static void consume(TokenType type, const char* message);

// Pratts parser helper function
static ParseRule* getRule(TokenType type);

// Pratts parser helper function
static void parsePrecedence(Precedence precedence);

// Emit one byte
static void emitByte(uint8_t byte);

// Emit two bytes. Most of the instructions is 2-bytes-instruction
static void emitBytes(uint8_t byte1, uint8_t byte2);

// Create a constant using a given literal value
static uint8_t makeConstant(Value value);

// Emit constant instruction
static void emitConstant(Value value);

// Emit a constant for a given name
static uint8_t identifierConstant(Token* name);

// Add local variable
static void addLocal(Token name);

// Remove last added local varible
// Sometimes, honestly, once on this compiler, a variable in some contexts is
// considered as a reserved word.
static void removeLastLocal();

// Mark last local variable as initialized.
// This prevent ambiguous code like this:
//    var test = test + 1;
static void markLocalInitialized();

// Check if two identifiers tokens literals are the same.
static bool identifiersEqual(Token* a, Token* b);

// Declare a variable using previous consumed token.
static void declareVariable();

// Declare a variable using any token.
static void declareVariableUsingToken(Token* name);

// Parse a variable declaration.
static uint8_t parseVariable(const char* message);

// Define a variable
static void defineVariable(uint8_t global);

// Parse variable declaration
static void varDeclaration();

// Handle panic mode synchronization
static void synchronize();

// Begin a new block scope
static void beginScope();

// End current block scope
static void endScope();

// Parse a block of code
static void block();

// Parse any function
static void function(FunctionType type);

// Parse function declaration
static void funDeclaration();

// Parse method
static void method(Token* className);

// Synthetically create a token of a given type
static Token syntheticToken(TokenType type, const char* str);

// Parse class declaration
static void classDeclaration();

// Parse any declaration
static void declaration();

// Parse expression statement
static void expressionStatement();

// Emit any Jump instruction + 2-bytes bytecode padding
static int emitJump(OpCode instruction);

// Patch any jump instruction of any padding
//
// The majority of jumps are like this:
// "
//  OP_CODE
//  0xff
//  0xff
// "
// That is, the instruction and two bytes of offset which means padding = 2.
// Instructions like OP_TRY_CATCH are like:
// "
//  OP_CODE
//  0xff
//  0xff
//  0xff
//  0xff
//  0xff
// "
// That is, after the OP_CODE, there are 5 bytes of padding.
// In order to correctly calculate the offset we need the padding = 5.
static int patchJump(int jmp, uint8_t instructionPadding);

// Parse if statement
static void ifStatement();

// Emit loop instruction
static void emitLoop(int beforeLoop);

// Begin loop block
static void beginLoop();

// End loop block
static void endLoop();

// Begin switch block
static void beginSwitch();

// End switch block
static void endSwitch();

// Emit loop guard instruction
static int emitLoopGuard();

// Parse while statement
static void whileStatement();

// Add system local variable
// System local variables are helper variables used in statements such as
// "for element of elements" statement
static void addSystemLocalVariable();

// Parse ranged for statement
static void forInRangeStatement(int iterationVariableConstant);

// Parse for each statement or call forInRangeStatement
static void sugaredForStatement();

// Parse plain for statements or call sugaredForStatement
static void forStatement();

// Parse return statement
static void returnStatement();

// Compile an imported module
ObjModule* compileModule(ModuleNode* node, char* absPath, const char* source);

// Resolve an imported module using "modules", or compile it
ObjModule* resolveModule(char* absPath, const char* source);

// Parse import statement
static void importStatement();

// Parse export statement
static void exportStatement();

// Emit try-catch instruction
static int emitTryCatch();

// Patch try-catch instruction parameter flag
static void patchTryCatchParameter(int parameter, bool shouldReceive);

// Parse try-catch statement
static void tryStatement();

// Parse throw statement
static void throwStatement();

// Parse break statement
static void breakStatement();

// Parse switch statement
static void switchStatement();

// Parse any statements
static void statement();

// Parse any expression
static void expression();

// Parse grouping expression or lambda functions
static void grouping(bool canAssign);

// Parse unary operators
static void unary(bool canAssign);

// Parse binary operators
static void binary(bool canAssign);

// Parse number literal
static void number(bool canAssign);

// Parse true, false and nil literals
static void literal(bool canAssign);

// Escape previous string token literal
static ObjString* escapeString();

// Parse string interpoltion
static void stringInterpolation(bool canAssign);

// Parse plain string or call stringInterpolation
static void string(bool canAssign);

// Resolve local variable
static int resolveLocal(Compiler* compiler, Token* name);

// Add upvalue to current compiler
static int addUpValue(Compiler* compiler, uint8_t index, bool isLocal);

// Resolve variable identifier lookup
static void namedVariable(Token token, bool canAssign);

// Parse variable identifier
static void variable(bool canAssign);

// Parse logical "and" operator
static void _and(bool canAssign);

// Parse logical "or" operator
static void _or(bool canAssign);

// Parse call arguments list
static uint8_t argumentsList();

// Parse call
static void call(bool canAssign);

// Parse function expression
static void functionExpression(bool canAssign);

// Parse property access or property update
static void propertyGetOrSet(bool canAssign);

// Parse "this" expression
static void _this(bool canAssign);

// Parse array expression
static void array(bool canAssign);

// Parse "super" expression
static void _super(bool canAssign);

// Check if token literal is a valid identifier
bool isValidIdentifier(Token* token);

// Parse object literal
void object(bool canAssign);

// Modules graph
Modules modules;

// Compiler current parser
Parser parser;

// Current compiler
Compiler* current;

// Current class, if defined, compiler helper
ClassCompiler* currentClass;

// Entry file absolute path
char* basePath;

#define GLOBAL_VARIABLES() (current->scopeDepth == 0)

static Chunk* currentChunk() { return &current->function->chunk; }

static void initCompiler(Compiler* compiler, char* absPath, FunctionType type) {
  compiler->absPath = absPath;
  compiler->enclosing = current;
  compiler->semanticallyEnclosing = type == TYPE_MODULE ? NULL : current;
  compiler->function = newFunction();
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->blockStackCount = 0;
  compiler->hasExported = false;

  current = compiler;

  switch (type) {
    case TYPE_CONSTRUCTOR:
    case TYPE_FUNCTION:
    case TYPE_METHOD: {
      compiler->function->name =
          copyString(parser.previous.start, parser.previous.length);
      break;
    }
    case TYPE_MODULE: {
      compiler->function->name = copyString(absPath, strlen(absPath));
      break;
    }
    case TYPE_LAMBDA_FUNCTION: {
      compiler->function->name = vm.lambdaFunctionName;
      break;
    }
    case TYPE_SCRIPT:
      break;
  }

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;

  if (type == TYPE_METHOD || type == TYPE_CONSTRUCTOR) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static void emitReturn() {
  if (current->type == TYPE_CONSTRUCTOR) {
    emitBytes(OP_GET_LOCAL, (uint8_t)0);
  } else {
    emitByte(OP_NIL);
  }
  emitByte(OP_RETURN);
}

static ObjFunction* endCompiler() {
  emitReturn();
  ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), function->name != NULL
                                         ? function->name->chars
                                         : "<script>");
  }
#endif

  current = current->enclosing;

  return function;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;

  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) { errorAt(&parser.previous, message); }

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) {
      break;
    }

    errorAtCurrent(parser.current.start);
  }
}

static bool check(TokenType tokenType) {
  return parser.current.type == tokenType;
}

static bool match(TokenType type) {
  if (parser.current.type != type) return false;

  advance();
  return true;
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;

  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;

  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static uint8_t makeConstant(Value value) {
  int constantIdx = addConstant(currentChunk(), value);
  if (constantIdx > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constantIdx;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

static void removeLastLocal() { current->localCount--; }

static void markLocalInitialized() {
  if (GLOBAL_VARIABLES()) return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static void declareVariable() {
  if (GLOBAL_VARIABLES()) return;

  Token* name = &parser.previous;

  for (int idx = current->localCount - 1; idx >= 0; idx--) {
    Local* local = &current->locals[idx];

    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }
    if (identifiersEqual(name, &local->name)) {
      error("Already variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static void declareVariableUsingToken(Token* name) {
  if (GLOBAL_VARIABLES()) return;

  for (int idx = current->localCount - 1; idx >= 0; idx--) {
    Local* local = &current->locals[idx];

    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }
    if (identifiersEqual(name, &local->name)) {
      error("Already variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static uint8_t parseVariable(const char* message) {
  consume(TOKEN_IDENTIFIER, message);

  declareVariable();
  if (!GLOBAL_VARIABLES()) return 0;

  return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
  if (!GLOBAL_VARIABLES()) {
    markLocalInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

static void varDeclaration() {
  uint8_t global = parseVariable("Expect variable identifier.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }

  defineVariable(global);
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

static void synchronize() {
  parser.panicMode = false;
  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;

    switch (parser.previous.type) {
      case TOKEN_VAR:
      case TOKEN_FUN:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_FOR:
      case TOKEN_RETURN:
        return;
      default:
        break;
    }

    advance();
  }
}

static void beginScope() { current->scopeDepth++; }

static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    if (current->locals[current->localCount - 1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }

    current->localCount--;
  }
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' at end of block.");
}

static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, current->absPath, type);
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }
      uint8_t paramConstant = parseVariable("Expect parameter name.");
      defineVariable(paramConstant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parametrs.");

  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction* function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
  for (int idx = 0; idx < function->upvalueCount; idx++) {
    emitByte(compiler.upvalues[idx].index);
    emitByte(compiler.upvalues[idx].isLocal ? 1 : 0);
  }
}

static void funDeclaration() {
  uint8_t global = parseVariable("Expect function name.");
  markLocalInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}

static void method(Token* className) {
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t nameConstant = identifierConstant(&parser.previous);

  FunctionType type = TYPE_METHOD;

  if (parser.previous.length == className->length &&
      memcmp(className->start, parser.previous.start, className->length) == 0) {
    type = TYPE_CONSTRUCTOR;
  }
  function(type);
  emitBytes(OP_METHOD, nameConstant);
}

static Token syntheticToken(TokenType type, const char* str) {
  Token token;
  token.type = type;
  token.start = str;
  token.length = strlen(str);
  token.line = parser.previous.line;

  return token;
}

static void classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token name = parser.previous;
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitBytes(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.enclosing = currentClass;
  classCompiler.name = name;
  currentClass = &classCompiler;
  currentClass->hasSuperclass = false;

  if (match(TOKEN_EXTENDS)) {
    consume(TOKEN_IDENTIFIER, "Expect superclass name.");

    if (identifiersEqual(&name, &parser.previous)) {
      error("Class can't extend itself.");
    }

    beginScope();

    addLocal(syntheticToken(TOKEN_IDENTIFIER, "super"));
    markLocalInitialized();

    variable(false);
    namedVariable(name, false);
    emitByte(OP_INHERIT);

    currentClass->hasSuperclass = true;
  }

  namedVariable(name, false);
  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_EOF) && !check(TOKEN_RIGHT_BRACE)) {
    method(&name);
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
  emitByte(OP_POP);

  if (currentClass->hasSuperclass) {
    endScope();
  }

  currentClass = currentClass->enclosing;
}

static void declaration() {
  if (match(TOKEN_CLASS)) {
    classDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else if (match(TOKEN_FUN)) {
    funDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

static int emitJump(OpCode instruction) {
  writeChunk(currentChunk(), instruction, parser.previous.line);
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  return currentChunk()->count - 2;
}

static int patchJump(int jmp, uint8_t instructionPadding) {
  int offset = currentChunk()->count - jmp - instructionPadding;
  if (offset > UINT16_MAX) {
    error("To much code to jump over.");
  }
  currentChunk()->code[jmp] = (offset >> 8) & 0xff;
  currentChunk()->code[jmp + 1] = offset & 0xff;
  return jmp + 2;
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' before if expresion");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after if expresion");

  int thenJmp = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  statement();

  int elseJmp = emitJump(OP_JUMP);

  patchJump(thenJmp, 2);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) {
    statement();
  }
  patchJump(elseJmp, 2);
}

static void emitLoop(int beforeLoop) {
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - beforeLoop + 2;
  if (offset > UINT16_MAX) {
    error("To much code to jump over.");
  }

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static void beginLoop() {
  current->blockStack[current->blockStackCount++].type = LOOP_BLOCK;
}

static void endLoop() { current->blockStackCount--; }

static void beginSwitch() {
  current->blockStack[current->blockStackCount++].type = SWITCH_BLOCK;
}

static void endSwitch() { current->blockStackCount--; }

static int emitLoopGuard() {
  emitByte(OP_LOOP_GUARD);

  // - Loop start address offset
  // When we have an incrementer, the start of the loop is the incrementer and
  // the loop jumps are like this:
  //
  // 1) incrementer -> comparison -> body -> (incrementer | end)
  //
  // Otherwise, the start of the loop is the comparison and the loop jumps are
  // like this:
  //
  // 2) comparison -> body -> (comparison | end)
  //
  // The 2° scenario is handled by default, since the OP_LOOP_GUARD instruction
  // is emitted right before the comparison. For the 1° scenario, we need to
  // patch this offset, to start just before the incrementer.
  writeChunk(currentChunk(), 0x00, parser.previous.line);
  writeChunk(currentChunk(), 0x00, parser.previous.line);

  // Loop end address offset
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  return currentChunk()->count - 4;
}

static void whileStatement() {
  beginLoop();

  int loopGuard = emitLoopGuard() + 2;
  int loopStart = currentChunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expect '(' before if expresion");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after if expresion");

  int exitLoop = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);

  statement();

  emitLoop(loopStart);
  patchJump(loopGuard, 2);
  patchJump(exitLoop, 2);

  emitByte(OP_LOOP_GUARD_END);
  emitByte(OP_POP);

  endLoop();
}

static void addSystemLocalVariable() {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  local->name = syntheticToken(TOKEN_IDENTIFIER, "");
  ;
  local->depth = current->scopeDepth;
  local->isCaptured = false;
}

static void forInRangeStatement(int iterationVariableConstant) {
  if (iterationVariableConstant < 0) {
    // if "for range(...)", then last local variable
    // "range" should be considered as reserved word
    removeLastLocal();
  } else {
    Token rangeSyntheticToken = syntheticToken(TOKEN_IDENTIFIER, "range");

    consume(TOKEN_IDENTIFIER, "Expect 'range' for range-based for.");
    if (!identifiersEqual(&rangeSyntheticToken, &parser.previous)) {
      error("Expect 'range' for range-based for.");
    }

    defineVariable(iterationVariableConstant);
  }

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'range'.");

  expression();

  if (match(TOKEN_COMMA)) {
    expression();

    if (match(TOKEN_COMMA)) {
      expression();
    } else {
      emitBytes(OP_CONSTANT, makeConstant(NIL_VAL));
    }
  } else {
    emitBytes(OP_CONSTANT, makeConstant(NIL_VAL));
    emitBytes(OP_CONSTANT, makeConstant(NIL_VAL));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");

  // manually declaring ranged-base for variables.
  // We keep the expression results variable on the stack
  // until we reach the end of the scope.
  //
  // track "start" variable conditionaly.
  // if "for range(...)", then "start" is a system variable and
  // must be tracked. Otherwise, it is a user defined variable.
  if (iterationVariableConstant < 0) addSystemLocalVariable();
  // track "end" variable
  addSystemLocalVariable();
  // track "step" variable
  addSystemLocalVariable();

  emitByte(OP_RANGED_LOOP_SETUP);
  int loopGuard = emitLoopGuard() + 2;
  int loopStart = currentChunk()->count;
  emitByte(OP_RANGED_LOOP);

  statement();

  emitLoop(loopStart);
  patchJump(loopGuard, 2);
  emitByte(OP_LOOP_GUARD_END);

  // Normal loops are usualy compiled like this:
  //
  //  <START>
  //  <EXPRESSION>
  //  OP_JUMP_IF_FALSE     => <END>
  //  OP_POP
  //  ...
  //  OP_JUMP              => <START>
  //  <END>
  //  OP_POP
  //
  // The last OP_POP is used to remove the OP_JUMP_IF_FALSE <EXPRESSION> from
  // the stack.
  //
  // The LOOP_GUARD implementation is built on top of this concept and whenever
  // it faces a "break" statement, it adds a dummy value to be consumed by the
  // OP_POP after <END>.
  //
  // Although we dont have any explict expression in this for-each, in order to
  // comply with the LOOP_GUARD implementation, we will be popping the dummy
  // value it adds. We will also be adding this dummy value where needed. pop
  // dummy value
  emitByte(OP_POP);

  endScope();
  endLoop();
}

static void sugaredForStatement() {
  uint8_t iterationVariableConstant =
      parseVariable("Expect for each iteration variable identifier.");
  Token rangeSyntheticToken = syntheticToken(TOKEN_IDENTIFIER, "range");

  if (identifiersEqual(&parser.previous, &rangeSyntheticToken)) {
    // handle "for range(...)"
    forInRangeStatement(-1);
  } else if (match(TOKEN_IN)) {
    // handle "for idx of range(...)"
    forInRangeStatement(iterationVariableConstant);
  } else {
    // handle "for element of elements"

    consume(TOKEN_OF, "Expect 'of' after for each iteration variable.");

    // manually declaring user iteration name variable
    defineVariable(iterationVariableConstant);
    uint8_t iterationName = makeConstant(NIL_VAL);
    emitBytes(OP_CONSTANT, iterationName);

    // Below we declare auxiliary variables for the OP_NAMED_LOOP
    // These variables are cleared once the scope is closed

    // manually declaring system iteration index variable
    emitBytes(OP_CONSTANT, makeConstant(NUMBER_VAL(-1)));
    addSystemLocalVariable();

    // manually declaring system iterator variable.
    // We keep the expression result variable on the stack
    // until we reach the end of the scope.
    expression();
    addSystemLocalVariable();

    int loopGuard = emitLoopGuard() + 2;
    int loopStart = currentChunk()->count;

    emitByte(OP_NAMED_LOOP);

    statement();

    emitLoop(loopStart);

    patchJump(loopGuard, 2);
    emitByte(OP_LOOP_GUARD_END);

    // Normal loops are usualy compiled like this:
    //
    //  <START>
    //  <EXPRESSION>
    //  OP_JUMP_IF_FALSE     => <END>
    //  OP_POP
    //  ...
    //  OP_JUMP              => <START>
    //  <END>
    //  OP_POP
    //
    // The last OP_POP is used to remove the OP_JUMP_IF_FALSE <EXPRESSION> from
    // the stack.
    //
    // The LOOP_GUARD implementation is built on top of this concept and
    // whenever it faces a "break" statement, it adds a dummy value to be
    // consumed by the OP_POP after <END>.
    //
    // Although we dont have any explict expression in this for-each, in order
    // to comply with the LOOP_GUARD implementation, we will be popping the
    // dummy value it adds. We will also be adding this dummy value where
    // needed. pop dummy value
    emitByte(OP_POP);

    // The manually declared iteration name is popped from the stack as soon as
    // the block is closed
    endScope();
    endLoop();
  }
}

static void forStatement() {
  beginLoop();
  beginScope();

  if (check(TOKEN_IDENTIFIER)) {
    sugaredForStatement();
    return;
  }

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  if (match(TOKEN_SEMICOLON)) {
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

  int loopGuard = emitLoopGuard();
  int loopStart = currentChunk()->count;

  int exitJmp = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after 'for' condition.");

    exitJmp = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJmp = emitJump(OP_JUMP);

    loopGuard = patchJump(loopGuard, 4);
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJmp, 2);
  } else {
    loopGuard += 2;
  }

  statement();
  emitLoop(loopStart);
  patchJump(loopGuard, 2);
  if (exitJmp != -1) {
    patchJump(exitJmp, 2);
  }
  emitByte(OP_LOOP_GUARD_END);
  emitByte(OP_POP);

  endScope();
  endLoop();
}

static void returnStatement() {
  if (current->semanticallyEnclosing == NULL) {
    error("Cannot return outside a function.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    if (current->type == TYPE_CONSTRUCTOR) {
      error("Can't return a value from a constructor.");
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return expression.");
    emitByte(OP_RETURN);
  }
}

ObjModule* compileModule(ModuleNode* node, char* path, const char* source) {
  Compiler compiler;
  Lexer moduleLexer;
  Parser moduleParser;
  Parser previousParser = parser;

  initCompiler(&compiler, path, TYPE_MODULE);

  moduleParser.module = node;
  moduleParser.hadError = parser.hadError;
  moduleParser.panicMode = false;
  parser = moduleParser;

  stackLexer(&moduleLexer, source);
  beginScope();

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction* function = (ObjFunction*)GCWhiteList((Obj*)endCompiler());
  ObjModule* module = newModule(function);
  GCPopWhiteList();

  ObjModule* returnValue = parser.hadError ? NULL : module;

  if (parser.hadError) {
    fprintf(stderr, "at file: %s\n", path);
  }

  parser = previousParser;
  popLexer();

  return returnValue;
}

ObjModule* resolveModule(char* absPath, const char* source) {
  ModuleNode* node = NULL;

  if (findModuleNode(&modules, parser.module, &node, absPath)) {
    // create dependecy between modules
    createDependency(parser.module, node);

    // module already compiled and can be reused
    return node->module;
  } else {
    // create a module node in COMPILING_STATE
    createModuleNode(&node, absPath);

    // create dependecy between modules
    createDependency(parser.module, node);

    // compile source code
    ObjModule* module = compileModule(node, absPath, source);

    // resolve module node to COMPILED_STATE
    resolveModuleNode(node, module);

    return module;
  }
}

static void importStatement() {
  int constant = -1;

  if (!check(TOKEN_STRING)) {
    constant = parseVariable("Expect import identifier name.");
    consume(TOKEN_FROM, "Expect 'from' after import identifier.");
  }
  consume(TOKEN_STRING, "Expect import path.");

  ObjString* importName =
      copyString(parser.previous.start + 1, parser.previous.length - 2);
  ObjModule* module;
  Value dummyValue;

  if (tableGet(&vm.nativeModules, importName, &dummyValue)) {
    // resolve module as Native module
    // Native modules are not computed on the dependency tree
    
    module = newNativeModule(importName);
  } else {
    // resolve module as User module

    // resolve path to always be absolute and hence unique
    char* absPath = resolvePath(basePath, current->absPath, importName->chars);
    // load module source code
    char* source = readFile(absPath);
    // compile module and arrange module dependencies
    module = resolveModule(absPath, source);

    if (module == NULL) {
      error("Cannot compile module.");
      return;
    }

    free(source);
    free(absPath);
  }

  uint8_t moduleConstant = makeConstant(OBJ_VAL(module));
  emitBytes(OP_IMPORT, moduleConstant);
  if (constant != -1) {
    defineVariable(constant);
  } else {
    emitByte(OP_POP);
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after import statement.");
}

static void exportStatement() {
  if (current->type != TYPE_MODULE) {
    errorAtCurrent("Expect export statement at the top level of a module.");
    return;
  }
  if (current->hasExported) {
    error("Modules can only export once.");
    return;
  }

  expression();
  emitByte(OP_EXPORT);
  current->hasExported = true;

  consume(TOKEN_SEMICOLON, "Expect ';' after export statement.");
}

static int emitTryCatch() {
  writeChunk(currentChunk(), OP_TRY_CATCH, parser.previous.line);
  // catch offset
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  // outside block offset
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  // bool that indicates if catch expects to receive a parameter
  writeChunk(currentChunk(), 0xff, parser.previous.line);
  return currentChunk()->count - 5;
}

static void patchTryCatchParameter(int parameter, bool shouldReceive) {
  // patch bool that indicates if catch expects to receive a parameter
  currentChunk()->code[parameter] = shouldReceive;
}

static void tryStatement() {
  int tryCatch = emitTryCatch(OP_TRY_CATCH);
  int catchParameterConstant = -1;

  statement();
  emitByte(OP_TRY_CATCH_TRY_END);

  tryCatch = patchJump(tryCatch, 5);

  consume(TOKEN_CATCH, "Expect 'catch' after try statement.");
  beginScope();
  if (match(TOKEN_LEFT_PAREN)) {
    catchParameterConstant = parseVariable("Expect catch parameter.");
    defineVariable(catchParameterConstant);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after catch parameter.");
  }
  consume(TOKEN_LEFT_BRACE, "Expect '{' after 'catch' statement.");

  block();
  endScope();

  tryCatch = patchJump(tryCatch, 3);
  patchTryCatchParameter(tryCatch, catchParameterConstant != -1);
}

static void throwStatement() {
  expression();
  emitByte(OP_THROW);
  consume(TOKEN_SEMICOLON, "Expect ';' after throw statement.");
}

static void breakStatement() {
  if (current->blockStackCount == 0) {
    error("Unexpected 'break' statement.");
    return;
  }

  if (current->blockStack[current->blockStackCount - 1].type == LOOP_BLOCK) {
    emitByte(OP_LOOP_BREAK);
  } else {
    emitByte(OP_SWITCH_BREAK);
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after break statement.");
}

static void continueStatement() {
  bool hasEnclosingLoop = false;

  for (int idx = current->blockStackCount - 1; idx >= 0; idx--) {
    if (current->blockStack[idx].type == LOOP_BLOCK) {
      hasEnclosingLoop = true;
      break;
    }
  }

  if (!hasEnclosingLoop) {
    error("Cannot continue outside a loop.");
    return;
  }

  emitByte(OP_LOOP_CONTINUE);
  consume(TOKEN_SEMICOLON, "Expect ';' after continue statement.");
}

static void switchStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' before switch expression.");

  beginSwitch();
  beginScope();
  expression();
  addSystemLocalVariable();
  int switchJump = emitJump(OP_SWITCH);
  int defaultStart = -1;

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after switch expression.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before switch body.");

  while (match(TOKEN_CASE) || match(TOKEN_DEFAULT)) {
    OpCode instruction =
        parser.previous.type == TOKEN_CASE ? OP_SWITCH_CASE : OP_SWITCH_DEFAULT;

    if (instruction == OP_SWITCH_CASE) {
      expression();

      consume(TOKEN_COLON, "Expect ':' after case expression.");

      while (match(TOKEN_CASE)) {
        expression();
        consume(TOKEN_COLON, "Expect ':' after case expression.");
      }
      int caseJump = emitJump(OP_SWITCH_CASE);
      statement();
      patchJump(caseJump, 2);
    } else {
      if (defaultStart != -1) {
        errorAt(&parser.previous,
                "Expect 'default' to appear just once in switch body.");
      }

      consume(TOKEN_COLON, "Expect ':' after case expression.");

      // Emit jump to skip default statement during the switch case execution
      int jump = emitJump(OP_JUMP);
      // Save start of default statement so that we can LOOP back if no case is
      // met
      defaultStart = currentChunk()->count;
      statement();
      // Patch jump to skip default statement during the switch case execution
      patchJump(jump, 2);
    }
  }

  patchJump(switchJump, 2);
  int defaultOffset =
      defaultStart != -1 ? currentChunk()->count - defaultStart + 3 : 0;

  if (defaultOffset > UINT16_MAX) {
    error("To much code to jump over.");
  }

  emitByte(OP_SWITCH_END);
  // Emit offset to LOOP back to default statement
  emitBytes((defaultOffset >> 8) & 0xff, defaultOffset & 0xff);
  endScope();
  endSwitch();

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after switch body.");
}

static void statement() {
  if (match(TOKEN_CONTINUE)) {
    continueStatement();
  } else if (match(TOKEN_BREAK)) {
    breakStatement();
  } else if (match(TOKEN_THROW)) {
    throwStatement();
  } else if (match(TOKEN_TRY)) {
    tryStatement();
  } else if (match(TOKEN_IMPORT)) {
    importStatement();
  } else if (match(TOKEN_EXPORT)) {
    exportStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else if (match(TOKEN_SWITCH)) {
    switchStatement();
  } else {
    expressionStatement();
  }
}

static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);

  // Compile ternary operator
  if (match(TOKEN_QUESTION_MARK)) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    expression();
    int thenJump = emitJump(OP_JUMP);

    consume(TOKEN_COLON, "Expect ':' for ternary operator.");

    patchJump(elseJump, 2);

    emitByte(OP_POP);
    expression();

    patchJump(thenJump, 2);
  }
}

ObjFunction* compile(const char* source, char* absPath) {
  // compile is called with absPath = NULL if vm is in repl mode
  // or if wer are compiling native modules 
  bool hasModulesSupport = absPath != NULL;

  Compiler compiler;
  initLexer(source);
  initCompiler(&compiler, absPath, TYPE_SCRIPT);
  if (hasModulesSupport) initModules(&modules, absPath);

  basePath = absPath;
  parser.module = modules.root;
  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction* function = (ObjFunction*)GCWhiteList((Obj*)endCompiler());
  if (hasModulesSupport) freeModules(&modules);
  GCPopWhiteList();

  if (parser.hadError) {
    fprintf(stderr, "at file: %s\n", basePath);
    return NULL;
  }

  return function;
}

void markCompilerRoots() {
  Compiler* compiler = current;
  while (compiler != NULL) {
    markObject((Obj*)compiler->function);
    compiler = compiler->enclosing;
  }
}

static void grouping(bool canAssign) {
  // Parse () -> {}
  if (match(TOKEN_RIGHT_PAREN)) {
    Compiler compiler;
    initCompiler(&compiler, current->absPath, TYPE_LAMBDA_FUNCTION);

    consume(TOKEN_MINUS, "Expect '-' for anonymous function.");
    consume(TOKEN_GREATER, "Expect '>' for anonymous function.");

    if (match(TOKEN_LEFT_BRACE)) {
      beginScope();
      block();
    } else {
      expression();
      emitByte(OP_RETURN);
    }

    ObjFunction* function = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
    for (int idx = 0; idx < function->upvalueCount; idx++) {
      emitByte(compiler.upvalues[idx].index);
      emitByte(compiler.upvalues[idx].isLocal);
    }
  } else if (match(TOKEN_IDENTIFIER)) {
    Token name = parser.previous;

    if (check(TOKEN_COMMA)) {
      // Parse (a, b, ...) -> {}
      Compiler compiler;
      initCompiler(&compiler, current->absPath, TYPE_LAMBDA_FUNCTION);
      beginScope();

      // parse TOKEN_IDENTIFIER
      declareVariable();
      markLocalInitialized();
      // ----

      match(TOKEN_COMMA);

      if (!check(TOKEN_RIGHT_PAREN)) {
        do {
          current->function->arity++;
          if (current->function->arity > 255) {
            errorAtCurrent("Can't haave more than 255 parameters.");
          }
          defineVariable(parseVariable("Expect parameter name."));
        } while (match(TOKEN_COMMA));
      }

      consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments list.");
      consume(TOKEN_MINUS, "Expect '-' for anonymous function.");
      consume(TOKEN_GREATER, "Expect '>' for anonymous function.");

      if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
      } else {
        expression();
        emitByte(OP_RETURN);
      }

      ObjFunction* function = endCompiler();
      emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
      for (int idx = 0; idx < function->upvalueCount; idx++) {
        emitByte(compiler.upvalues[idx].index);
        emitByte(compiler.upvalues[idx].isLocal);
      }
    } else if (match(TOKEN_RIGHT_PAREN)) {
      if (match(TOKEN_MINUS)) {
        // Parse (a) -> {}
        Compiler compiler;
        initCompiler(&compiler, current->absPath, TYPE_LAMBDA_FUNCTION);
        beginScope();
        // parse TOKEN_IDENTIFIER
        declareVariableUsingToken(&name);
        markLocalInitialized();

        compiler.function->arity = 1;

        consume(TOKEN_GREATER, "Expect '>' for anonymous function.");

        if (match(TOKEN_LEFT_BRACE)) {
          beginScope();
          block();
        } else {
          expression();
          emitByte(OP_RETURN);
        }

        ObjFunction* function = endCompiler();
        emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
        for (int idx = 0; idx < function->upvalueCount; idx++) {
          emitByte(compiler.upvalues[idx].index);
          emitByte(compiler.upvalues[idx].isLocal);
        }
      } else {
        // Parse (a)
        namedVariable(name, canAssign);
      }
    } else {
      // Parse (a (op b)*)
      namedVariable(parser.previous, canAssign);

      while (PREC_ASSIGNMENT <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
      }

      if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
      }

      consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    }
  } else {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
  }
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
    case TOKEN_MINUS:
      emitByte(OP_NEGATE);
      break;
    case TOKEN_BANG:
      emitByte(OP_NOT);
      break;
    default:
      break;
  }
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_PLUS:
      emitByte(OP_ADD);
      break;
    case TOKEN_MINUS:
      emitByte(OP_SUBTRACT);
      break;
    case TOKEN_STAR:
      emitByte(OP_MULTIPLY);
      break;
    case TOKEN_SLASH:
      emitByte(OP_DIVIDE);
      break;
    case TOKEN_EQUAL_EQUAL:
      emitByte(OP_EQUAL);
      break;
    case TOKEN_BANG_EQUAL:
      emitBytes(OP_EQUAL, OP_NOT);
      break;
    case TOKEN_GREATER:
      emitByte(OP_GREATER);
      break;
    case TOKEN_LESS:
      emitByte(OP_LESS);
      break;
    case TOKEN_GREATER_EQUAL:
      emitBytes(OP_LESS, OP_NOT);
      break;
    case TOKEN_LESS_EQUAL:
      emitBytes(OP_GREATER, OP_NOT);
      break;
    default:
      return;
  }
}

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_TRUE:
      emitByte(OP_TRUE);
      break;
    case TOKEN_FALSE:
      emitByte(OP_FALSE);
      break;
    case TOKEN_NIL:
      emitByte(OP_NIL);
      break;
    default:
      return;
  }
}

static ObjString* escapeString() {
  const char* str = parser.previous.start + 1;
  int length = parser.previous.length - 2;
  int scapedLength = 0;

  for (int idx = 0; idx < length; idx++) {
    if (str[idx] == '\\') {
      if (idx + 1 < length && str[idx + 1] == '\\') {
        scapedLength++;
        idx++;
      }

      continue;
    }
    scapedLength++;
  }

  char buffer[scapedLength];

  int count = 0;
  for (int idx = 0; idx < length; idx++) {
    if (str[idx] != '\\') {
      buffer[count++] = str[idx];
      continue;
    }

    switch (str[++idx]) {
      case 'n':
        buffer[count++] = '\n';
        break;
      case 't':
        buffer[count++] = '\t';
        break;
      case 'b':
        buffer[count++] = '\b';
        break;
      case 'r':
        buffer[count++] = '\r';
        break;
      case 'f':
        buffer[count++] = '\f';
        break;
      case 'v':
        buffer[count++] = '\v';
        break;
      case '0':
        buffer[count++] = '\0';
        break;
      default:
        buffer[count++] = str[idx];
    }
  }

  return copyString(buffer, scapedLength);
}

static void stringInterpolation(bool canAssign) {
  ObjString* template = (ObjString*)GCWhiteList((Obj*)escapeString());
  uint8_t placeholdersCount = 0;

  // Iterate through string template and scan/compile every placeholder
  // expression.
  for (int idx = 0; idx < template->length; idx++) {
    // template placeholder slot found
    if (template->chars[idx] == '$' && idx + 1 < template->length &&
        template->chars[idx + 1] == '(') {
      placeholdersCount++;

      if (placeholdersCount > UINT8_MAX) {
        error("Can't have more than 255 string interpolation placeholders.");
      }

      // skip "$(" chars
      idx += 2;
      int interpolationStart = idx;
      int isPlaceholderOpen = 1;

      // consume placeholder character until slot end
      while (isPlaceholderOpen > 0) {
        if (template->chars[idx] == '(') isPlaceholderOpen++;
        if (template->chars[idx] == ')') isPlaceholderOpen--;
        idx++;
      }
      idx--;

      // setup lexer and parser
      Lexer interpolationLexer;
      Parser interpolationParser;
      Parser previousParser = parser;

      interpolationParser.module = parser.module;
      interpolationParser.hadError = parser.hadError;
      interpolationParser.panicMode = false;
      parser = interpolationParser;

      // setup source code (placeholder literal)
      int sourceLength = idx - interpolationStart;
      char source[sourceLength + 1];

      for (int i = 0; i < sourceLength; i++) {
        source[i] = template->chars[interpolationStart + i];
      }
      source[sourceLength] = '\0';

      // start lexer and parser
      stackLexer(&interpolationLexer, source);
      advance();

      // compile expected expression
      expression();

      // if there are more than one expression, throw error
      if (!isAtEnd()) {
        error("Invalid string interpolation expression.");
      }

      // setup previous lexer and parser back
      popLexer();
      parser = previousParser;
    }
  }

  GCPopWhiteList();
  emitBytes(OP_STRING_INTERPOLATION, makeConstant(OBJ_VAL(template)));
}

static void string(bool canAssign) { emitConstant(OBJ_VAL(escapeString())); }

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int idx = compiler->localCount - 1; idx >= 0; idx--) {
    Local* local = &compiler->locals[idx];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Cannot resolve local variable in its own initializer.");
      }
      return idx;
    }
  }

  return -1;
}

static int addUpValue(Compiler* compiler, uint8_t index, bool isLocal) {
  int upvaluesCount = compiler->function->upvalueCount;

  for (int idx = upvaluesCount - 1; idx >= 0; idx--) {
    if (compiler->upvalues[idx].index == index &&
        compiler->upvalues[idx].isLocal == isLocal) {
      return idx;
    }
  }

  if (upvaluesCount == UINT8_COUNT) {
    error("Too many closure variables in a function.");
    return 0;
  }

  compiler->upvalues[upvaluesCount].index = index;
  compiler->upvalues[upvaluesCount].isLocal = isLocal;

  return compiler->function->upvalueCount++;
}

static int resolveUpValue(Compiler* compiler, Token* name) {
  if (compiler->semanticallyEnclosing == NULL) return -1;

  int local = resolveLocal(compiler->semanticallyEnclosing, name);
  if (local != -1) {
    compiler->semanticallyEnclosing->locals[local].isCaptured = true;
    return addUpValue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpValue(compiler->semanticallyEnclosing, name);
  if (upvalue != -1) {
    return addUpValue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

static void namedVariable(Token token, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &token);

  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpValue(current, &token)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&token);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  } else if (canAssign && match(TOKEN_PLUS_EQUAL)) {
    emitBytes(getOp, (uint8_t)arg);
    expression();
    emitByte(OP_ADD);
    emitBytes(setOp, (uint8_t)arg);
  } else if (canAssign && match(TOKEN_MINUS_EQUAL)) {
    emitBytes(getOp, (uint8_t)arg);
    expression();
    emitByte(OP_SUBTRACT);
    emitBytes(setOp, (uint8_t)arg);
  } else if (canAssign && match(TOKEN_STAR_EQUAL)) {
    emitBytes(getOp, (uint8_t)arg);
    expression();
    emitByte(OP_MULTIPLY);
    emitBytes(setOp, (uint8_t)arg);
  } else if (canAssign && match(TOKEN_SLASH_EQUAL)) {
    emitBytes(getOp, (uint8_t)arg);
    expression();
    emitByte(OP_DIVIDE);
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void _and(bool canAssign) {
  int shortCircuitJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(shortCircuitJump, 2);
}

static void _or(bool canAssign) {
  int shortCircuitJump = emitJump(OP_JUMP_IF_FALSE);
  int jump = emitJump(OP_JUMP);

  patchJump(shortCircuitJump, 2);

  emitByte(OP_POP);
  parsePrecedence(PREC_OR);

  patchJump(jump, 2);
}

static uint8_t argumentsList() {
  int argCount = 0;

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();

      if (argCount > 255) {
        error("Can't have more than 255 arguments.");
      }

      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments list.");

  return argCount;
}

static void call(bool canAssign) {
  uint8_t argCount = argumentsList();
  emitBytes(OP_CALL, argCount);
}

static void functionExpression(bool canAssign) {
  function(TYPE_LAMBDA_FUNCTION);
}

static void propertyGetOrSet(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name.");
  uint8_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  } else if (canAssign && match(TOKEN_PLUS_EQUAL)) {
    emitBytes(OP_GET_PROPERTY, name);
    emitByte(true);
    expression();
    emitByte(OP_ADD);
    emitBytes(OP_SET_PROPERTY, name);
  } else if (canAssign && match(TOKEN_MINUS_EQUAL)) {
    emitBytes(OP_GET_PROPERTY, name);
    emitByte(true);
    expression();
    emitByte(OP_SUBTRACT);
    emitBytes(OP_SET_PROPERTY, name);
  } else if (canAssign && match(TOKEN_STAR_EQUAL)) {
    emitBytes(OP_GET_PROPERTY, name);
    emitByte(true);
    expression();
    emitByte(OP_MULTIPLY);
    emitBytes(OP_SET_PROPERTY, name);
  } else if (canAssign && match(TOKEN_SLASH_EQUAL)) {
    emitBytes(OP_GET_PROPERTY, name);
    emitByte(true);
    expression();
    emitByte(OP_DIVIDE);
    emitBytes(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t args = argumentsList();
    emitBytes(OP_INVOKE, name);
    emitByte(args);
  } else {
    emitBytes(OP_GET_PROPERTY, name);
    emitByte(false);
  }
}

static void _this(bool canAssign) {
  if (currentClass == NULL) {
    error("Can't access 'this' keyword outside of a class.");
    return;
  }

  variable(false);
}

static void array(bool canAssign) {
  int length = 0;

  if (!check(TOKEN_RIGHT_BRACKET)) {
    do {
      expression();

      if (length > 255) {
        error("Can't initialize array with more than 255 elements.");
      }

      length++;
    } while (match(TOKEN_COMMA));
  }

  emitBytes(OP_ARRAY, length);
  consume(TOKEN_RIGHT_BRACKET, "Expect ']' at end of array expression.");
}

static void arrayGetOrSet(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_BRACKET, "Expect ']' at end of array access.");

  // Should array item invocation be optimized?
  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitByte(OP_SET_ITEM);
  } else if (canAssign && match(TOKEN_PLUS_EQUAL)) {
    emitBytes(OP_GET_ITEM, (uint8_t) true);
    expression();
    emitByte(OP_ADD);
    emitByte(OP_SET_ITEM);
  } else if (canAssign && match(TOKEN_MINUS_EQUAL)) {
    emitBytes(OP_GET_ITEM, (uint8_t) true);
    expression();
    emitByte(OP_SUBTRACT);
    emitByte(OP_SET_ITEM);
  } else if (canAssign && match(TOKEN_STAR_EQUAL)) {
    emitBytes(OP_GET_ITEM, (uint8_t) true);
    expression();
    emitByte(OP_MULTIPLY);
    emitByte(OP_SET_ITEM);
  } else if (canAssign && match(TOKEN_SLASH_EQUAL)) {
    emitBytes(OP_GET_ITEM, (uint8_t) true);
    expression();
    emitByte(OP_DIVIDE);
    emitByte(OP_SET_ITEM);
  } else {
    // When assign operators other than '=' are used, we need to keep the base +
    // identifier in the stack. The bool is intended to handle that.
    emitBytes(OP_GET_ITEM, (uint8_t) false);
  }
}

static void _super(bool canAssign) {
  if (currentClass == NULL) {
    error("Expect super inside a class.");
  } else if (!currentClass->hasSuperclass) {
    error("Expect class to have a superclass.");
  }

  consume(TOKEN_DOT, "Expect '.' after super.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name after '.'.");
  uint8_t name = identifierConstant(&parser.previous);

  // Should this be optmized for calls?
  namedVariable(syntheticToken(TOKEN_IDENTIFIER, "this"), false);
  namedVariable(syntheticToken(TOKEN_IDENTIFIER, "super"), false);
  emitBytes(OP_SUPER, name);
}

bool isValidIdentifier(Token* token) { return isAlpha(*token->start); }

void object(bool canAssign) {
  int count = 0;

  if (!check(TOKEN_RIGHT_BRACE)) {
    do {
      if (!isValidIdentifier(&parser.current)) {
        error("Expect object property identifier.");
      }

      advance();

      uint8_t name = identifierConstant(&parser.previous);
      
      emitBytes(OP_CONSTANT, name);
      if (match(TOKEN_COLON)) {
        expression();
      } else {
        namedVariable(parser.previous, false);
      }
      
      count++;

      if (count > 255) {
        error(
            "Can't initialize more than 255 properties in an object literal.");
      }
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' at the end of the object.");

  emitBytes(OP_OBJECT, (uint8_t)count);
}

// Pratts parser helper parsing table
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {array, arrayGetOrSet, PREC_PRIMARY},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {object, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, propertyGetOrSet, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_MINUS_MINUS] = {unary, NULL, PREC_UNARY},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_PLUS_PLUS] = {unary, NULL, PREC_UNARY},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_STRING_INTERPOLATION] = {stringInterpolation, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, _and, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {functionExpression, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, _or, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {_super, NULL, PREC_NONE},
    [TOKEN_THIS] = {_this, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_FROM] = {NULL, NULL, PREC_NONE},
    [TOKEN_EXPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }