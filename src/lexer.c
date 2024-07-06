#include "lexer.h"

#include <stdio.h>
#include <string.h>

#include "common.h"

Lexer globalLexer;
Lexer* lexer;

void initLexer(const char* source) {
  lexer = &globalLexer;
  lexer->start = source;
  lexer->current = source;
  lexer->line = 1;
};

void stackLexer(Lexer* nextLexer, const char* source) {
  nextLexer->start = source;
  nextLexer->current = source;
  nextLexer->line = 1;
  nextLexer->enclosing = lexer;

  lexer = nextLexer;
}

void popLexer() { lexer = lexer->enclosing; }

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = lexer->start;
  token.length = (int)(lexer->current - lexer->start);
  token.line = lexer->line;

  return token;
}

static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = lexer->line;

  return token;
}

static bool isAtEnd() { return *lexer->current == '\0'; }

static char peek() { return *lexer->current; }

static char peekNext() {
  if (isAtEnd()) return '\0';
  return lexer->current[1];
}

static char advance() {
  lexer->current++;
  return lexer->current[-1];
}

static bool match(char c) {
  if (isAtEnd()) return false;
  if (peek() != c) return false;

  advance();
  return true;
}

static void skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        lexer->line++;
        advance();
        break;
      case '/':
        if (peekNext() == '/') {
          while (peek() != '\n' && !isAtEnd()) advance();
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static Token string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') lexer->line++;
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  advance();
  return makeToken(TOKEN_STRING);
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static TokenType checkKeyword(int start, int length, char* rest,
                              TokenType type) {
  if (lexer->current - lexer->start == start + length &&
      memcmp(lexer->start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
  switch (lexer->start[0]) {
    case 'a':
      return checkKeyword(1, 2, "nd", TOKEN_AND);
    case 'b':
      return checkKeyword(1, 4, "reak", TOKEN_BREAK);
    case 'e':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'l':
            return checkKeyword(2, 2, "se", TOKEN_ELSE);
          case 'x':
            return checkKeyword(2, 4, "port", TOKEN_EXPORT);
        }
        break;
      }
    default:
    case 'i':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'm':
            return checkKeyword(2, 4, "port", TOKEN_IMPORT);
          case 'f':
            return checkKeyword(2, 0, "", TOKEN_IF);
        }
        break;
      }
    case 'n':
      return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'o':
      return checkKeyword(1, 1, "r", TOKEN_OR);
    case 'p':
      return checkKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r':
      return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 'v':
      return checkKeyword(1, 2, "ar", TOKEN_VAR);
    case 'w':
      return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    case 's':
      return checkKeyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'r':
            if (lexer->current - lexer->start > 2) {
              switch(lexer->start[2]) {
                case 'y':
                  return checkKeyword(3, 0, "", TOKEN_TRY);
                case 'u':
                  return checkKeyword(3, 1, "e", TOKEN_TRUE); 
              }
              break;
            }
          case 'h':
            if (lexer->current - lexer->start > 2) {
              switch(lexer->start[2]) {
                case 'i':
                  return checkKeyword(3, 0, "s", TOKEN_THIS);
                case 'r':
                  return checkKeyword(3, 2, "ow", TOKEN_THROW); 
              }
              break;
            }
        }
        break;
      }
    case 'c':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'a':
            return checkKeyword(2, 3, "tch", TOKEN_CATCH); 
          case 'l':
            return checkKeyword(2, 3, "ass", TOKEN_CLASS);
          case 'o':
            return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
        }
        break;
      }
    case 'f':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'a':
            return checkKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o':
            return checkKeyword(2, 1, "r", TOKEN_FOR);
          case 'u':
            return checkKeyword(2, 1, "n", TOKEN_FUN);
          case 'r':
            return checkKeyword(2, 2, "om", TOKEN_FROM);
        }
        break;
      }
  }

  return TOKEN_IDENTIFIER;
}

static Token identifier() {
  while (isAlpha(peek()) || isDigit(peek())) advance();
  return makeToken(identifierType());
}

static Token number() {
  while (isDigit(peek()) && !isAtEnd()) {
    advance();
  }

  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek()) && !isAtEnd()) {
      advance();
    }
  }

  return makeToken(TOKEN_NUMBER);
}

Token scanToken() {
  skipWhitespace();

  lexer->start = lexer->current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if (isDigit(c)) return number();
  if (isAlpha(c)) return identifier();

  switch (c) {
    case '(':
      return makeToken(TOKEN_LEFT_PAREN);
    case ')':
      return makeToken(TOKEN_RIGHT_PAREN);
    case '{':
      return makeToken(TOKEN_LEFT_BRACE);
    case '}':
      return makeToken(TOKEN_RIGHT_BRACE);
    case '[':
      return makeToken(TOKEN_LEFT_BRACKET);
    case ']':
      return makeToken(TOKEN_RIGHT_BRACKET);
    case ';':
      return makeToken(TOKEN_SEMICOLON);
    case ',':
      return makeToken(TOKEN_COMMA);
    case '.':
      return makeToken(TOKEN_DOT);
    case '-':
      return makeToken(match('=') ? TOKEN_MINUS_EQUAL : match('-') ? TOKEN_MINUS_MINUS : TOKEN_MINUS);
    case '+':
      return makeToken(match('=') ? TOKEN_PLUS_EQUAL : match('+') ? TOKEN_PLUS_PLUS : TOKEN_PLUS);
    case '/':
      return makeToken(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
    case '*':
      return makeToken(match('=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
    case '!':
      return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"':
      return string();
  }

  return errorToken("Unexpected character.");
};