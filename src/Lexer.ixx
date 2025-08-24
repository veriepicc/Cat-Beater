export module Lexer;

import Token;
import <string>;
import <vector>;
import <stdexcept>;
import <variant>;

export class Lexer {
public:
    Lexer(const std::string& source, int startLine = 1)
        : source(source), start(0), current(0), line(startLine), col(1), tokenStartCol(1) {}

    std::vector<Token> scanTokens() {
        while (!isAtEnd()) {
            start = current;
            tokenStartCol = col;
            scanToken();
        }
        tokens.push_back(Token{ TokenType::END_OF_FILE, "", {}, line, col });
        return tokens;
    }

private:
    bool isAtEnd() const { return current >= source.length(); }
    char advance() { char ch = source[current++]; col++; return ch; }
    char peek() const { return isAtEnd() ? '\0' : source[current]; }
    char peekNext() const { return (current + 1 >= source.length()) ? '\0' : source[current + 1]; }

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }
    static bool isAlpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool isAlphaNumeric(char c) { return isAlpha(c) || isDigit(c); }
    static bool isHexDigit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    void addToken(TokenType type) { tokens.push_back(Token{ type, source.substr(start, current - start), {}, line, tokenStartCol }); }
    void addToken(TokenType type, const std::variant<std::monostate, double, std::string>& lit) {
        tokens.push_back(Token{ type, source.substr(start, current - start), lit, line, tokenStartCol });
    }

    void scanToken() {
        tokenStartCol = col;
        char c = advance();
        switch (c) {
        case '(': addToken(TokenType::LEFT_PAREN); break;
        case ')': addToken(TokenType::RIGHT_PAREN); break;
        case '[': addToken(TokenType::LEFT_BRACKET); break;
        case ']': addToken(TokenType::RIGHT_BRACKET); break;
        case '{': addToken(TokenType::LEFT_BRACE); break;
        case '}': addToken(TokenType::RIGHT_BRACE); break;
        case '.': /* sentence terminator, ignore in lexer */ break;
        case '+': addToken(TokenType::PLUS); break;
        case '-':
            if (peek() == '>') {
                advance(); // consume '>'
                addToken(TokenType::ARROW);
            } else {
                addToken(TokenType::MINUS);
            }
            break;
        case '*': addToken(TokenType::ASTERISK); break;
        case '%': addToken(TokenType::MODULO); break;
        case '/': 
            if (peek() == '/') {
                // C-style line comment: // until EOL
                advance(); // consume second /
                while (peek() != '\n' && !isAtEnd()) advance();
            } else if (peek() == '*') {
                // C-style block comment: /* ... */
                advance(); // consume *
                blockComment();
            } else {
                addToken(TokenType::SLASH);
            }
            break;
        case ',': addToken(TokenType::COMMA); break;
        case '>': addToken(peek() == '=' ? (advance(), TokenType::GREATER_EQUAL) : TokenType::GREATER); break;
        case '<': addToken(peek() == '=' ? (advance(), TokenType::LESS_EQUAL) : TokenType::LESS); break;
        case '!': addToken(peek() == '=' ? (advance(), TokenType::BANG_EQUAL) : TokenType::BANG); break;
        case '=': addToken(peek() == '=' ? (advance(), TokenType::EQUAL_EQUAL) : TokenType::EQUAL); break;
        case '&': 
            if (peek() == '&') {
                advance(); // consume second &
                addToken(TokenType::AND);
            } else {
                addToken(TokenType::AMPERSAND);
            }
            break;
        case '|':
            if (peek() == '|') {
                advance(); // consume second |
                addToken(TokenType::OR);
            } else {
                throw std::runtime_error(std::string("Unexpected character '|' at line ") + std::to_string(line) + ", col " + std::to_string(col - 1) + ". Did you mean '||'?");
            }
            break;
        case ';':
            // Always treat ';' as a semicolon token for concise syntax.
            // English-style ';' line comments are still filtered earlier at the line level.
            addToken(TokenType::SEMICOLON);
            break;
        case '#':
            // Line comment: # until EOL (legacy compatibility)
            while (peek() != '\n' && !isAtEnd()) advance();
            break;
        case ':': addToken(TokenType::COLON); break; // now generate token for concise syntax
        case 'λ': addToken(TokenType::ARROW); break; // allow unicode lambda as arrow (optional aesthetic)
        case ' ': case '\r': case '\t': break; // ignore
        case '\n': line++; col = 1; break;
        case '"': string(); break;
        default:
            if (isDigit(c)) number();
            else if (isAlpha(c)) identifier();
            else throw std::runtime_error(std::string("Unexpected character '") + c + "' at line " + std::to_string(line) + ", col " + std::to_string(col - 1) + ".");
        }
    }

    void identifier() {
        while (isAlphaNumeric(peek())) advance();
        std::string text = source.substr(start, current - start);
        if (text == "and") { addToken(TokenType::AND); return; }
        if (text == "or") { addToken(TokenType::OR); return; }
        addToken(TokenType::IDENTIFIER);
    }

    void number() {
        // Hex literal: 0x... or 0X...
        if (source[start] == '0' && (peek() == 'x' || peek() == 'X')) {
            advance(); // consume x/X
            size_t hexStart = current;
            while (isHexDigit(peek())) advance();
            if (current == hexStart) throw std::runtime_error(std::string("Invalid hex literal at line ") + std::to_string(line) + ", col " + std::to_string(tokenStartCol) + ".");
            std::string hexDigits = source.substr(hexStart, current - hexStart);
            unsigned long long v = 0;
            try {
                v = std::stoull(hexDigits, nullptr, 16);
            } catch (...) {
                throw std::runtime_error(std::string("Hex literal out of range at line ") + std::to_string(line) + ", col " + std::to_string(tokenStartCol) + ".");
            }
            addToken(TokenType::NUMBER_LITERAL, static_cast<double>(v));
            return;
        }

        while (isDigit(peek())) advance();
        if (peek() == '.' && isDigit(peekNext())) {
            advance();
            while (isDigit(peek())) advance();
        }
        addToken(TokenType::NUMBER_LITERAL, std::stod(source.substr(start, current - start)));
    }

    void string() {
        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') { line++; col = 0; }
            advance();
        }
        if (isAtEnd()) throw std::runtime_error(std::string("Unterminated string at line ") + std::to_string(line) + ", col " + std::to_string(tokenStartCol) + ".");
        advance();
        std::string value = source.substr(start + 1, current - start - 2);
        addToken(TokenType::STRING_LITERAL, value);
    }

    void blockComment() {
        // Handle /* ... */ block comments
        while (!isAtEnd()) {
            if (peek() == '*' && peekNext() == '/') {
                advance(); // consume *
                advance(); // consume /
                return;
            }
            if (peek() == '\n') { line++; col = 1; }
            advance();
        }
        throw std::runtime_error(std::string("Unterminated block comment at line ") + std::to_string(line) + ", col " + std::to_string(tokenStartCol) + ".");
    }

    const std::string source;
    std::vector<Token> tokens;
    size_t start;
    size_t current;
    int line;
    int col;
    int tokenStartCol;
};


