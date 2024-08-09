<h1 align="center">Simpl</h1>

<br>

<p>&nbsp;&nbsp;&nbsp;&nbsp; <b>Simpl</b> is a simple OOP <a href="https://dev.to/lexplt/understanding-bytecode-interpreters-eig">bytecode script language</a>.</p>

<p><b>Simpl</b> has a <a href="https://github.com/saymow/simpl/tree/master/tests-runner/src" target="_blank">test runner</a>, hundreds of <a href="https://github.com/saymow/simpl/blob/master/tests/language/ternary-operator.simpl">test files<a/> and few scripts to run <a href="https://github.com/saymow/simpl/blob/master/tests/benchmark/fib.simpl">benchmarks</a>.</p>

<p>You can get a feel for <b>Simpl</b> syntax by looking at this lexer implementation: </p>

```
class Lexer {
    Lexer(source) {
        this.source = source;
        this.idx = 0;
        this.startIdx = -1;
        this.line = 1;
        this.tokens = [];
        this.Identifier = {
            and: TokenType.AND,
            class: TokenType.CLASS,
            extends: TokenType.EXTENDS,
            else: TokenType.ELSE,
            false: TokenType.FALSE,
            for: TokenType.FOR,
            fun: TokenType.FUN,
            if: TokenType.IF,
            nil: TokenType.NIL,
            or: TokenType.OR,
            print: TokenType.PRINT,
            return: TokenType.RETURN,
            super: TokenType.SUPER,
            this: TokenType.THIS,
            true: TokenType.TRUE,
            var: TokenType.VAR,
            while: TokenType.WHILE,
            import: TokenType.IMPORT,
            from: TokenType.FROM,
            export: TokenType.EXPORT,
            try: TokenType.TRY,
            catch: TokenType.CATCH,
            throw: TokenType.THROW,
            break: TokenType.BREAK,
            continue: TokenType.CONTINUE,
            switch: TokenType.SWITCH,
            case: TokenType.CASE,
            default: TokenType.DEFAULT,
            of: TokenType.OF
        };
    }

    addToken(tokenType, literal) {
        this.tokens.push({
            type: tokenType,
            lexeme: this.source.substr(this.startIdx, this.idx),
            literal: literal,
            line: this.line,
            startIdx: this.startIdx,
            length: this.idx - this.startIdx
        });
    }

    number() {
        while (!this.atEnd() and this.isDigit(this.peek())) this.advance();
    
        if (this.peek() == "." and this.isDigit(this.peekNext())) {
            this.advance();
            while (!this.atEnd() and this.isDigit(this.peek())) this.advance();
        }

        return this.addToken(
            TokenType.NUMBER, 
            Number.toNumber(this.source.substr(this.startIdx, this.idx))
        );
    }

    identifier() {
        while (!this.atEnd() and this.isAlphaNumeric(this.peek())) this.advance();
        var lexeme = this.source.substr(this.startIdx, this.idx);

        if (this.Identifier[lexeme] != nil) {
            return this.addToken(this.Identifier[lexeme], nil);
        }

        return this.addToken(TokenType.IDENTIFIER, nil);
    }

    string() {
        while (!this.atEnd() and this.peek() != "\"") {
            if (this.peek() == "\\n") this.line += 1;
            if (this.peek() == "\\" and this.peekNext() == "\"") this.advance();

            this.advance(); 
        }
        
        this.advance();

        if (this.atEnd()) {
            throw Error("Unterminated string");
        }

        return this.addToken(TokenType.STRING, nil);
    }

    scan() {
        var char = this.advance();
        
        switch (char) {
            case "(":
                return this.addToken(TokenType.LEFT_PAREN, nil);
            case ")":
                return this.addToken(TokenType.RIGHT_PAREN, nil);
            case "[":
                return this.addToken(TokenType.LEFT_BRACKET, nil);
            case "]":
                return this.addToken(TokenType.RIGHT_BRACKET, nil);
            case "{":
                return this.addToken(TokenType.LEFT_BRACE, nil);
            case "}":
                return this.addToken(TokenType.RIGHT_BRACE, nil);
            case ",":
                return this.addToken(TokenType.COMMA, nil);
            case ".":
                return this.addToken(TokenType.DOT, nil);
            case "?":
                return this.addToken(TokenType.QUESTION_MARK, nil);
            case ":":
                return this.addToken(TokenType.COLON, nil);
            case ";":
                return this.addToken(TokenType.SEMICOLON, nil);
            case "+": 
                return this.addToken(this.match("=") ? TokenType.PLUS_EQUAL : TokenType.PLUS, nil);
            case "-": 
                return this.addToken(this.match("=") ? TokenType.MINUS_EQUAL : TokenType.MINUS, nil);
            case "*": 
                return this.addToken(this.match("=") ? TokenType.STAR_EQUAL : TokenType.STAR, nil);
            case "/": 
                return this.addToken(this.match("=") ? TokenType.SLASH_EQUAL : TokenType.SLASH, nil);
            case "!":
                return this.addToken(this.match("=") ? TokenType.BANG_EQUAL : TokenType.BANG, nil);
            case "=":
                return this.addToken(this.match("=") ? TokenType.EQUAL_EQUAL : TokenType.EQUAL, nil);
            case ">":
                return this.addToken(this.match("=") ? TokenType.GREATER_EQUAL : TokenType.GREATER, nil);
            case "<":
                return this.addToken(this.match("=") ? TokenType.LESS_EQUAL : TokenType.LESS, nil);
            case "\"":
                return this.string();
            default: {
                if (this.isDigit(char)) {
                    return this.number(char);
                } else if (this.isAlpha(char)) {
                    return this.identifier(char);
                }
            }
        }

        throw Error("Unexpected token.");
    }

    execute() {
        var len = this.source.length();

        while (this.idx < len) {
            this.startIdx = this.idx;
            this.scan();
        }

        return this.tokens;
    }

    isAlphaNumeric(char) {
        return this.isAlpha(char) or this.isDigit(char) or char == "_";
    }

    isAlpha(char) {
        return (char.charCodeAt(0) >= 97 and char.charCodeAt(0) <= 122) or
            (char.charCodeAt(0) >= 65 and char.charCodeAt(0) <= 90); 
    }

    isDigit(char) {
        return Number.toNumber(char) != nil;
    }

    match(char) {
        if (this.atEnd() or this.peek() != char) return false;
        this.advance();
        return true;
    }

    peek() {
        return this.source[this.idx];
    }

    peekNext() {
        if (this.idx + 1 >= this.source.length()) return nil;
        return this.source[this.idx + 1];
    }

    advance() {
        if (this.atEnd()) return nil;

        var char = this.source[this.idx];
        this.idx += 1;

        return char;
    }

    atEnd() {
        return this.idx >= this.source.length();
    }
}
```
