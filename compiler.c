#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"
#include "memory.h"
#include "object.h"
#include "utils.h"
#include "vm.h"
#include "modules.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

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

typedef struct ClassCompiler {
  struct ClassCompiler* enclosing;
  Token name;
} ClassCompiler;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  ModuleNode* module;
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} UpValue;

typedef struct Compiler {
  ObjFunction* function;
  FunctionType type;

  Local locals[UINT8_MAX];
  int localCount;
  int scopeDepth;
  UpValue upvalues[UINT8_MAX];

  struct Compiler* enclosing;
  struct Compiler* semanticallyEnclosing;
  FunctionType topLevelType;
} Compiler;

static void emitByte(uint8_t byte);
static void emitBytes(uint8_t byte1, uint8_t byte2);
static void declaration();
static void statement();
static void expression();
static void emitByte(uint8_t byte);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void namedVariable(Token token, bool canAssign);
static void string(bool canAssign);

Modules modules;
Parser parser;
Compiler* current;
ClassCompiler* currentClass;

#define GLOBAL_VARIABLES() \
  (current->scopeDepth == 0 && current->type != TYPE_MODULE)

static Chunk* currentChunk() { return &current->function->chunk; }

static void initCompiler(Compiler* compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->semanticallyEnclosing = type == TYPE_MODULE ? NULL : current;
  compiler->function = newFunction();
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;

  if (type == TYPE_SCRIPT || type == TYPE_MODULE) {
    compiler->topLevelType = type;
  } else {
    compiler->topLevelType = current->topLevelType;
  }

  if (type != TYPE_SCRIPT) {
    compiler->function->name =
        copyString(parser.previous.start, parser.previous.length);
  }

  current = compiler;

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

  // todo: print module file name 
  // if (!parser.hadError) {
  //   printf("filename.in\n");
  // }

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
      case TOKEN_PRINT:
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
  initCompiler(&compiler, type);
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

  namedVariable(name, false);
  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_EOF) && !check(TOKEN_RIGHT_BRACE)) {
    method(&name);
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
  emitByte(OP_POP);

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

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
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

static void patchJump(int jmp) {
  int offset = currentChunk()->count - jmp - 2;
  if (offset > UINT16_MAX) {
    error("To much code to jump over.");
  }
  currentChunk()->code[jmp] = (offset >> 8) & 0xff;
  currentChunk()->code[jmp + 1] = offset & 0xff;
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' before if expresion");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after if expresion");

  int thenJmp = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  statement();

  int elseJmp = emitJump(OP_JUMP);

  patchJump(thenJmp);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) {
    statement();
  }
  patchJump(elseJmp);
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

static void whileStatement() {
  int loopStart = currentChunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expect '(' before if expresion");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after if expresion");

  int exitLoop = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  statement();
  emitLoop(loopStart);

  patchJump(exitLoop);
  emitByte(OP_POP);
}

static void forStatement() {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  if (match(TOKEN_SEMICOLON)) {
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

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

    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJmp);
  }

  statement();
  emitLoop(loopStart);

  if (exitJmp != -1) {
    patchJump(exitJmp);
    emitByte(OP_POP);
  }

  endScope();
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

ObjModule* compileModule(ModuleNode* node, const char* source) {
  Compiler compiler;
  Lexer moduleLexer;
  Parser moduleParser;
  Parser previousParser = parser;

  moduleParser.module = node;
  moduleParser.hadError = parser.hadError;
  moduleParser.panicMode = false;
  parser = moduleParser;

  stackLexer(&moduleLexer, source);
  initCompiler(&compiler, TYPE_MODULE);
  beginScope();

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjModule* module = newModule(endCompiler());
  ObjModule* returnValue = parser.hadError ? NULL : module;

  parser = previousParser;
  popLexer();

  return returnValue;
}

ObjModule* resolveModule(const char* source) {
  ModuleNode* node = NULL;

  if (addDependency(&modules, parser.module, &node, source)) {
    // module already compiled and can be reused
    return node->module;
  } else {
    // create a module in COMPILING_STATE
    createModuleNode(&modules, parser.module, &node, source);
    // compile source code
    ObjModule* module = compileModule(node, source);
    // resolve module to COMPILED_STATE
    resolveDependency(&modules, node, module);

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

  ObjString* path =
      copyString(parser.previous.start + 1, parser.previous.length - 2);
  char* source = readFile(path->chars);
  ObjModule* module = resolveModule(source);

  if (module == NULL) {
    error("Cannot compile module.");
    return;
  }

  uint8_t moduleConstant = makeConstant(OBJ_VAL(module));

  module->function->name = path;

  free(source);
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
  consume(TOKEN_IDENTIFIER, "Expect name.");
  uint8_t constant = identifierConstant(&parser.previous);

  namedVariable(parser.previous, false);
  emitBytes(OP_EXPORT, constant);
  consume(TOKEN_SEMICOLON, "Expect ';' after export statement.");
}

static void statement() {
  if (match(TOKEN_IMPORT)) {
    importStatement();
  } else if (match(TOKEN_EXPORT)) {
    exportStatement();
  } else if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else {
    expressionStatement();
  }
}

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

ObjFunction* compile(const char* source) {
  Compiler compiler;
  initLexer(source);
  initCompiler(&compiler, TYPE_SCRIPT);
  initModules(&modules, source);

  parser.module = modules.root;
  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }


  ObjFunction* function = endCompiler();

  freeModules(&modules);

  return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
  Compiler* compiler = current;
  while (compiler != NULL) {
    markObject((Obj*)compiler->function);
    compiler = compiler->enclosing;
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
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

static void string(bool canAssign) {
  emitConstant(OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

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
    if (current->topLevelType == TYPE_MODULE) {
      // todo: print variable name
      error("Undefined variable.");
      return;
    }

    arg = identifierConstant(&token);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
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

  patchJump(shortCircuitJump);
}

static void _or(bool canAssign) {
  int shortCircuitJump = emitJump(OP_JUMP_IF_FALSE);
  int jump = emitJump(OP_JUMP);

  patchJump(shortCircuitJump);

  emitByte(OP_POP);
  parsePrecedence(PREC_OR);

  patchJump(jump);
}

static uint8_t argumentsList() {
  int8_t argCount = 0;

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
  function(TYPE_FUNCTION_EXPRESSION);
}

static void propertyGetOrSet(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name.");
  uint8_t name = identifierConstant(&parser.previous);

  if (match(TOKEN_EQUAL) && canAssign) {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t args = argumentsList();
    emitBytes(OP_INVOKE, name);
    emitByte(args);
  } else {
    emitBytes(OP_GET_PROPERTY, name);
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
    } while(match(TOKEN_COMMA));
  }

  emitBytes(OP_ARRAY, length);
  consume(TOKEN_RIGHT_BRACKET, "Expect ']' at end of array expression.");
}

static void arrayGetOrSet(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_BRACKET, "Expect ']' at end of array access.");

  // Should array item invocation be optimized? 
  if (match(TOKEN_EQUAL) && canAssign) {
    expression();
    emitByte(OP_SET_ITEM);
  } else {
    emitByte(OP_GET_ITEM);
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {array, arrayGetOrSet, PREC_PRIMARY},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, propertyGetOrSet, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
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
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
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