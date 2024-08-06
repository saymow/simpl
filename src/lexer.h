#ifndef lexer_h
#define lexer_h

#include "common.h"

typedef enum {
  // Single-character tokens.
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_QUESTION_MARK,
  TOKEN_COLON,
  TOKEN_SEMICOLON,
  // One or two character tokens.
  TOKEN_MINUS,
  TOKEN_MINUS_MINUS,
  TOKEN_PLUS,
  TOKEN_PLUS_PLUS,
  TOKEN_SLASH,
  TOKEN_STAR,
  TOKEN_MINUS_EQUAL,
  TOKEN_PLUS_EQUAL,
  TOKEN_SLASH_EQUAL,
  TOKEN_STAR_EQUAL,
  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER,
  TOKEN_GREATER_EQUAL,
  TOKEN_LESS,
  TOKEN_LESS_EQUAL,
  // Literals.
  TOKEN_IDENTIFIER,
  TOKEN_STRING,
  TOKEN_STRING_INTERPOLATION,
  TOKEN_NUMBER,
  // Keywords.
  TOKEN_AND,
  TOKEN_CLASS,
  TOKEN_EXTENDS,
  TOKEN_ELSE,
  TOKEN_FALSE,
  TOKEN_FOR,
  TOKEN_FUN,
  TOKEN_IF,
  TOKEN_NIL,
  TOKEN_OR,
  TOKEN_PRINT,
  TOKEN_RETURN,
  TOKEN_SUPER,
  TOKEN_THIS,
  TOKEN_TRUE,
  TOKEN_VAR,
  TOKEN_WHILE,
  TOKEN_IMPORT,
  TOKEN_FROM,
  TOKEN_EXPORT,
  TOKEN_TRY,
  TOKEN_CATCH,
  TOKEN_THROW,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_SWITCH,
  TOKEN_CASE,
  TOKEN_DEFAULT,
  TOKEN_OF,
  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef struct Token {
  TokenType type;
  const char* start;
  int length;
  int line;
} Token;

typedef struct Lexer {
  struct Lexer* enclosing;
  const char* start;
  const char* current;
  int line;
} Lexer;

extern Lexer* lexer;

void initLexer(const char* source);
Token scanToken();
void stackLexer(Lexer* nextLexer, const char* source);
void popLexer();
bool isAlpha(char c);

#endif