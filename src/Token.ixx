export module Token;

import <string>;
import <variant>;

export enum class TokenType {
    // Single-character tokens
    LEFT_PAREN, RIGHT_PAREN,
    LEFT_BRACE, RIGHT_BRACE,
    COLON, SEMICOLON, ASTERISK, AMPERSAND, COMMA,
    LEFT_BRACKET, RIGHT_BRACKET,

    // Keywords
    FN, LET, LET_MUT, SET, IF, ELSE,
    WHILE,
    RETURN,

    // Literals
    IDENTIFIER, NUMBER_LITERAL, STRING_LITERAL, NIL,

    // Operators
    PLUS, MINUS, SLASH, MODULO,
    EQUAL, EQUAL_EQUAL,
    BANG, BANG_EQUAL,
    LESS, LESS_EQUAL,
    GREATER, GREATER_EQUAL,
    ARROW,
    AND, OR,

    // Types
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    BOOL,
    PTR,

    // End of file
    END_OF_FILE
};

export struct Token {
    TokenType type;
    std::string lexeme;
    std::variant<std::monostate, double, std::string> literal;
    int line;
    int col;
    Token() : type(TokenType::END_OF_FILE), lexeme(""), literal(std::monostate{}), line(0), col(0) {}
    Token(TokenType t, std::string lx, std::variant<std::monostate, double, std::string> lit, int ln, int cl = 0)
        : type(t), lexeme(std::move(lx)), literal(std::move(lit)), line(ln), col(cl) {}
};
