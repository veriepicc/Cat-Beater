export module Lexer;

import Token;
import <string>;
import <vector>;
import <stdexcept>;
import <variant>;
import <string_view>;

export class Lexer {
public:
    Lexer(const std::string& source, int startLine = 1)
        : source(source), start(0), current(0), line(startLine), col(1), tokenStartCol(1) {
        tokens.reserve(source.length() / 8);
    }

    std::vector<Token> scanTokens() {
        const char* data = source.data();
        const size_t length = source.length();

        while (current < length) {
            start = current;
            tokenStartCol = col;
            scanToken(data, length);
        }
        tokens.emplace_back(TokenType::END_OF_FILE, "", std::monostate{}, line, col);
        return tokens;
    }

private:
    static constexpr bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }
    static constexpr bool isAlpha(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static constexpr bool isAlphaNumeric(char c) noexcept { return isAlpha(c) || isDigit(c); }
    static constexpr bool isHexDigit(char c) noexcept {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    inline char advance(const char* data) noexcept { col++; return data[current++]; }
    inline char peek(const char* data, size_t length) const noexcept {
        return current >= length ? '\0' : data[current];
    }
    inline char peekNext(const char* data, size_t length) const noexcept {
        return (current + 1 >= length) ? '\0' : data[current + 1];
    }

    void addToken(TokenType type) {
        tokens.emplace_back(type, source.substr(start, current - start),
            std::monostate{}, line, tokenStartCol);
    }

    void addToken(TokenType type, const std::variant<std::monostate, double, std::string>& lit) {
        tokens.emplace_back(type, source.substr(start, current - start),
            lit, line, tokenStartCol);
    }

    void scanToken(const char* data, size_t length) {
        tokenStartCol = col;
        char c = advance(data);

        switch (c) {
        case '(': addToken(TokenType::LEFT_PAREN); break;
        case ')': addToken(TokenType::RIGHT_PAREN); break;
        case '[': addToken(TokenType::LEFT_BRACKET); break;
        case ']': addToken(TokenType::RIGHT_BRACKET); break;
        case '{': addToken(TokenType::LEFT_BRACE); break;
        case '}': addToken(TokenType::RIGHT_BRACE); break;
        case '.': break;
        case '+': addToken(TokenType::PLUS); break;
        case '-': addToken(TokenType::MINUS); break;
        case '*': addToken(TokenType::ASTERISK); break;
        case '%': addToken(TokenType::MODULO); break;
        case '/':
            if (peek(data, length) == '/') {
                current++;
                col++;
                skipLineComment(data, length);
            }
            else if (peek(data, length) == '*') {
                current++;
                col++;
                blockComment(data, length);
            }
            else {
                addToken(TokenType::SLASH);
            }
            break;
        case ',': addToken(TokenType::COMMA); break;
        case '>':
            if (peek(data, length) == '=') {
                current++; col++;
                addToken(TokenType::GREATER_EQUAL);
            }
            else {
                addToken(TokenType::GREATER);
            }
            break;
        case '<':
            if (peek(data, length) == '=') {
                current++; col++;
                addToken(TokenType::LESS_EQUAL);
            }
            else {
                addToken(TokenType::LESS);
            }
            break;
        case '!':
            if (peek(data, length) == '=') {
                current++; col++;
                addToken(TokenType::BANG_EQUAL);
            }
            else {
                addToken(TokenType::BANG);
            }
            break;
        case '=':
            if (peek(data, length) == '=') {
                current++; col++;
                addToken(TokenType::EQUAL_EQUAL);
            }
            else {
                addToken(TokenType::EQUAL);
            }
            break;
        case '&':
            if (peek(data, length) == '&') {
                current++; col++;
                addToken(TokenType::AND);
            }
            else {
                addToken(TokenType::AMPERSAND);
            }
            break;
        case '|':
            if (peek(data, length) == '|') {
                current++; col++;
                addToken(TokenType::OR);
            }
            else {
                throw std::runtime_error("Unexpected character '|' at line " + std::to_string(line) +
                    ", col " + std::to_string(col - 1) + ". Did you mean '||'?");
            }
            break;
        case ';':
            if (tokenStartCol == 1) {
                skipLineComment(data, length);
            }
            else {
                addToken(TokenType::SEMICOLON);
            }
            break;
        case '#':
            skipLineComment(data, length);
            break;
        case ':': addToken(TokenType::COLON); break;
        case ' ': case '\r': case '\t': break;
        case '\n': line++; col = 1; break;
        case '"': string(data, length); break;
        default:
            if (isDigit(c)) {
                number(data, length);
            }
            else if (isAlpha(c)) {
                identifier(data, length);
            }
            else {
                throw std::runtime_error("Unexpected character '" + std::string(1, c) +
                    "' at line " + std::to_string(line) +
                    ", col " + std::to_string(col - 1) + ".");
            }
        }
    }

    void identifier(const char* data, size_t length) {
        while (current < length && isAlphaNumeric(data[current])) {
            current++;
            col++;
        }

        const char* textStart = data + start;
        size_t textLen = current - start;

        if (textLen == 3 && textStart[0] == 'a' && textStart[1] == 'n' && textStart[2] == 'd') {
            addToken(TokenType::AND);
        }
        else if (textLen == 2 && textStart[0] == 'o' && textStart[1] == 'r') {
            addToken(TokenType::OR);
        }
        else {
            addToken(TokenType::IDENTIFIER);
        }
    }

    void number(const char* data, size_t length) {
        if (data[start] == '0' && current < length && (data[current] == 'x' || data[current] == 'X')) {
            current++;
            col++;
            size_t hexStart = current;

            while (current < length && isHexDigit(data[current])) {
                current++;
                col++;
            }

            if (current == hexStart) {
                throw std::runtime_error("Invalid hex literal at line " + std::to_string(line) +
                    ", col " + std::to_string(tokenStartCol) + ".");
            }

            std::string hexDigits(data + hexStart, current - hexStart);
            unsigned long long v = 0;
            try {
                v = std::stoull(hexDigits, nullptr, 16);
            }
            catch (...) {
                throw std::runtime_error("Hex literal out of range at line " + std::to_string(line) +
                    ", col " + std::to_string(tokenStartCol) + ".");
            }
            addToken(TokenType::NUMBER_LITERAL, static_cast<double>(v));
            return;
        }

        while (current < length && isDigit(data[current])) {
            current++;
            col++;
        }

        if (current < length && data[current] == '.' &&
            (current + 1 < length) && isDigit(data[current + 1])) {
            current++;
            col++;
            while (current < length && isDigit(data[current])) {
                current++;
                col++;
            }
        }

        std::string numStr(data + start, current - start);
        addToken(TokenType::NUMBER_LITERAL, std::stod(numStr));
    }

    void string(const char* data, size_t length) {
        while (current < length && data[current] != '"') {
            if (data[current] == '\n') {
                line++;
                col = 0;
            }
            current++;
            col++;
        }

        if (current >= length) {
            throw std::runtime_error("Unterminated string at line " + std::to_string(line) +
                ", col " + std::to_string(tokenStartCol) + ".");
        }

        current++;
        col++;
        std::string value{ data + start + 1, current - start - 2 };
        addToken(TokenType::STRING_LITERAL, std::move(value));
    }

    void skipLineComment(const char* data, size_t length) {
        while (current < length && data[current] != '\n') {
            current++;
            col++;
        }
    }

    void blockComment(const char* data, size_t length) {
        while (current < length) {
            if (data[current] == '*' && (current + 1 < length) && data[current + 1] == '/') {
                current += 2;
                col += 2;
                return;
            }
            if (data[current] == '\n') {
                line++;
                col = 1;
            }
            else {
                col++;
            }
            current++;
        }
        throw std::runtime_error("Unterminated block comment at line " + std::to_string(line) +
            ", col " + std::to_string(tokenStartCol) + ".");
    }

    const std::string source;
    std::vector<Token> tokens;
    size_t start;
    size_t current;
    int line;
    int col;
    int tokenStartCol;
};