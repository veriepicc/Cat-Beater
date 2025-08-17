export module Parser;

import Token;
import Type;
import Expr;
import Stmt;
import <vector>;
import <memory>;
import <stdexcept>;
import <string>;

// A minimal English-ish expression parser for calculator input like: 3+1*2
// Grammar (Pratt-style precedence):
// expression -> equality
// equality   -> comparison ( (== | !=) comparison )*
// comparison -> term ( ( > | >= | < | <= ) term )*
// term       -> factor ( ( + | - ) factor )*
// factor     -> unary ( ( * | / ) unary )*
// unary      -> ( - ) unary | primary
// primary    -> NUMBER | STRING | IDENTIFIER | '(' expression ')'

export class Parser {
public:
    Parser(const std::vector<Token>& tokens) : tokens(tokens) {}

    std::unique_ptr<Expr> parseExpression() { return expression(); }
    // Command-style statements: print X | add A and B | let NAME be VALUE | set NAME to VALUE | if/then/else/end | while/do/end | define function ...
    // Also supports concise syntax: print("hello"); | let x = 10; | if (cond) { ... } | fn name(args) { ... }
    std::unique_ptr<Stmt> parseCommand() {
        // Skip empty statements
        if (match({TokenType::SEMICOLON})) {
            return parseCommand(); // try again
        }
        
        // Check for clearly concise-only syntax first
        
        // fn NAME(PARAMS) { BODY } - unambiguously concise
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "fn") {
            advance(); // consume "fn"
            return parseConciseFunction();
        }
        
        // if (CONDITION) { THEN } [else { ELSE }] - unambiguously concise (English uses "if X then")
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "if") {
            int saved = current;
            advance(); // consume "if"
            if (check(TokenType::LEFT_PAREN)) {
                return parseConciseIf();
            } else {
                // Not concise if, restore and fall through to English
                current = saved;
            }
        }
        
        // while (CONDITION) { BODY } - unambiguously concise (English uses "while X do")
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "while") {
            int saved = current;
            advance(); // consume "while"
            if (check(TokenType::LEFT_PAREN)) {
                return parseConciseWhile();
            } else {
                // Not concise while, restore and fall through to English
                current = saved;
            }
        }
        
        // Check for ambiguous patterns that could be concise or English
        
        // let IDENT = EXPR; vs let IDENT be EXPR
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "let") {
            int saved = current;
            advance(); // consume "let"
            if (check(TokenType::IDENTIFIER)) {
                Token name = advance();
                if (check(TokenType::EQUAL)) {
                    // Concise: let x = value
                    advance(); // consume "="
                    auto value = expression();
                    match({TokenType::SEMICOLON}); // optional semicolon
                    return std::make_unique<LetStmt>(name, std::make_unique<PrimitiveType>(Token{ TokenType::F64, "f64", {}, 0 }), std::move(value));
                } else {
                    // English: let x be value - restore and fall through
                    current = saved;
                }
            } else {
                // Malformed, restore and fall through
                current = saved;
            }
        }
        
        // IDENT = EXPR; (concise assignment)
        if (check(TokenType::IDENTIFIER)) {
            int saved = current;
            Token name = advance();
            if (check(TokenType::EQUAL)) {
                advance(); // consume "="
                auto value = expression();
                match({TokenType::SEMICOLON}); // optional semicolon
                return std::make_unique<SetStmt>(name, std::move(value));
            } else {
                // Not an assignment, restore and continue
                current = saved;
            }
        }
        
        // Any remaining expression followed by semicolon (like function calls)
        if (!check(TokenType::END_OF_FILE) && !check(TokenType::RIGHT_BRACE)) {
            int saved = current;
            try {
                auto expr = expression();
                if (match({TokenType::SEMICOLON})) {
                    // This was an expression statement with semicolon
                    return std::make_unique<ExpressionStmt>(std::move(expr));
                } else {
                    // No semicolon, restore and fall through to English parsing
                    current = saved;
                }
            } catch (...) {
                // Expression parsing failed, restore and fall through
                current = saved;
            }
        }
        
        // Fall back to English parsing for all existing forms
        // exit N
        if (matchWord("exit")) {
            auto code = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(code));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__exit", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // array reserve / clear and map clear
        if (matchWord("reserve")) {
            auto arr = expression(); matchWord("by"); auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(arr)); args.push_back(std::move(n));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__array_reserve", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("clear")) {
            if (matchWord("map")) {
                auto m = expression();
                std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(m));
                return std::make_unique<ExpressionStmt>(
                    std::make_unique<Call>(
                        std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_clear", {}, 0 }),
                        Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                        std::move(args)
                    )
                );
            } else {
                auto a = expression();
                std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a));
                return std::make_unique<ExpressionStmt>(
                    std::make_unique<Call>(
                        std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__array_clear", {}, 0 }),
                        Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                        std::move(args)
                    )
                );
            }
        }
        // return EXPR
        if (matchWord("return")) {
            auto value = expression();
            return std::make_unique<ReturnStmt>(Token{ TokenType::RETURN, "return", {}, 0 }, std::move(value));
        }
        // define function name with parameters a, b returning number: BODY end
        if (matchWord("define")) {
            Token name; std::vector<Parameter> params; std::unique_ptr<Type> retType;
            if (!parseFunctionHeader(name, params, retType)) throw error(peek(), "Invalid function header.");
            // Body: either 'do' ... many commands ... 'end' OR single inline command then 'end'
            std::vector<std::unique_ptr<Stmt>> body;
            if (matchWord("do")) {
                while (!isAtEnd() && !isWordAhead("end")) {
                    body.push_back(parseCommand());
                }
                matchWord("end");
            } else {
                body.push_back(parseCommand());
                matchWord("end");
            }
            return std::make_unique<FunctionStmt>(name, std::move(params), std::move(retType), std::move(body));
        }
        // if CONDITION then (do BLOCK end | STATEMENT) [else/otherwise (do BLOCK end | STATEMENT)] end
        if (matchWord("if")) {
            auto cond = expression();
            matchWord("then");
            std::unique_ptr<Stmt> thenStmt;
            if (matchWord("do")) {
                std::vector<std::unique_ptr<Stmt>> block;
                while (!isAtEnd() && !isWordAhead("end") && !isWordAhead("else")) {
                    block.push_back(parseCommand());
                }
                matchWord("end");
                thenStmt = std::make_unique<BlockStmt>(std::move(block));
            } else {
                thenStmt = parseCommand();
            }
            std::unique_ptr<Stmt> elseStmt;
            if (matchWord("else") || matchWord("otherwise")) {
                if (matchWord("do")) {
                    std::vector<std::unique_ptr<Stmt>> eblock;
                    while (!isAtEnd() && !isWordAhead("end")) {
                        eblock.push_back(parseCommand());
                    }
                    matchWord("end");
                    elseStmt = std::make_unique<BlockStmt>(std::move(eblock));
                } else {
                    elseStmt = parseCommand();
                }
            }
            // consume trailing 'end' matching this if
            matchWord("end");
            return std::make_unique<IfStmt>(std::move(cond), std::move(thenStmt), std::move(elseStmt));
        }

        // while CONDITION do (BLOCK end | STATEMENT) end
        if (matchWord("while")) {
            auto cond = expression();
            matchWord("do");
            std::unique_ptr<Stmt> body;
            if (matchWord("do")) {
                std::vector<std::unique_ptr<Stmt>> wblock;
                while (!isAtEnd() && !isWordAhead("end")) {
                    wblock.push_back(parseCommand());
                }
                matchWord("end");
                body = std::make_unique<BlockStmt>(std::move(wblock));
            } else {
                body = parseCommand();
                matchWord("end");
            }
            return std::make_unique<WhileStmt>(std::move(cond), std::move(body));
        }
        if (matchWord("print") || matchWord("say")) {
            // print supports multiple arguments; each argument is a standard expression
            // Stop when we see a token that clearly begins another command on the same line/block
            auto nextStartsCommand = [&]() -> bool {
                static const char* words[] = {
                    "set", "let", "if", "while", "define", "append", "len", "return", "for", "else", "end"
                };
                for (const char* w : words) { if (isWordAhead(w)) return true; }
                return false;
            };
            std::vector<std::unique_ptr<Expr>> args;
            while (!isAtEnd()) {
                if (nextStartsCommand()) break;
                try { args.push_back(expression()); } catch (const std::exception&) { break; }
            }
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "print", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // join ARRAY by SEP
        if (matchWord("join")) {
            auto arr = expression();
            matchWord("by");
            auto sep = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(arr)); args.push_back(std::move(sep));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__join", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // trim S
        if (matchWord("trim")) {
            auto s = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(s));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__trim", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // replace S needle with repl
        if (matchWord("replace")) {
            auto s = expression();
            auto needle = expression();
            matchWord("with");
            auto repl = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(s)); args.push_back(std::move(needle)); args.push_back(std::move(repl));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__replace", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // call NAME with a and b (args separated by 'and' or ',')
        if (matchWord("call")) {
            Token fname = consume(TokenType::IDENTIFIER, "Expect function name after 'call'.");
            matchWord("with");
            std::vector<std::unique_ptr<Expr>> args;
            // parse first arg
            args.push_back(expression());
            // additional args separated by 'and' or ','
            while (match({ TokenType::COMMA }) || matchWord("and")) {
                args.push_back(expression());
            }
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(fname),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("set")) {
            // set key KEY of MAP to VALUE  (map assignment)
            if (isWordAhead("key")) {
                advance(); // consume 'key'
                auto keyExpr = expression();
                matchWord("of");
                auto mapExpr = expression();
                if (!matchWord("to")) { matchWord("equal"); matchWord("to"); }
                auto valExpr = expression();
                std::vector<std::unique_ptr<Expr>> args;
                args.push_back(std::move(mapExpr));
                args.push_back(std::move(keyExpr));
                args.push_back(std::move(valExpr));
                return std::make_unique<ExpressionStmt>(
                    std::make_unique<Call>(
                        std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_set", {}, 0 }),
                        Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                        std::move(args)
                    )
                );
            }
            // set NAME[IDX] to VALUE  OR  set NAME to VALUE
            if (check(TokenType::IDENTIFIER)) {
                Token name = advance();
                if (check(TokenType::LEFT_BRACKET)) {
                    advance();
                    auto idx = expression();
                    consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");
                    if (!matchWord("to")) { matchWord("at"); matchWord("index"); }
                    auto val = expression();
                    return std::make_unique<SetIndexStmt>(std::make_unique<Variable>(name), std::move(idx), std::move(val));
                } else {
                    if (!matchWord("to")) { matchWord("equal"); matchWord("to"); }
                    auto value = expression();
                    return std::make_unique<SetStmt>(name, std::move(value));
                }
            }
            // generic fallback: set <array-expr>[IDX] to VALUE
            auto arr = expression();
            consume(TokenType::LEFT_BRACKET, "Expect '[' after array expression.");
            auto idx = expression();
            consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");
            if (!matchWord("to")) { matchWord("at"); matchWord("index"); }
            auto val = expression();
            return std::make_unique<SetIndexStmt>(std::move(arr), std::move(idx), std::move(val));
        }
        // append VALUE to a
        if (matchWord("append") || matchWord("push")) {
            auto val = expression();
            if (!matchWord("to")) { matchWord("onto"); }
            auto arr = expression();
            // desugar to builtin call: __append(arr, val)
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(arr));
            args.push_back(std::move(val));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__append", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // pop from a  | remove last from a
        if (matchWord("pop") || (matchWord("remove") && matchWord("last"))) {
            if (!matchWord("from")) matchWord("of");
            auto arr = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(arr));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__pop", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // len a
        if (matchWord("len")) {
            auto arr = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(arr));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__len", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // Memory helpers: alloc N, free P, ptradd P by K, read8 P at K, write8 V to P at K, read16/32/64, write16/32/64, readf32/writef32, memcpy/memset, ptrdiff/realloc/blocksize/ptroffset/ptrblock
        if (matchWord("alloc")) {
            auto n = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(n));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__alloc", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("free")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__free", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("ptradd")) {
            auto p = expression(); matchWord("by"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_add", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("read8")) {
            auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__load8", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("write8")) {
            auto v = expression(); matchWord("to"); auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(v)); args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__store8", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("read32")) {
            auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__load32", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("write32")) {
            auto v = expression(); matchWord("to"); auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(v)); args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__store32", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("read16")) {
            auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__load16", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("write16")) {
            auto v = expression(); matchWord("to"); auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(v)); args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__store16", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("read64")) {
            auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__load64", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("write64")) {
            auto v = expression(); matchWord("to"); auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(v)); args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__store64", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("readf32")) {
            auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__loadf32", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("writef32")) {
            auto v = expression(); matchWord("to"); auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(v)); args.push_back(std::move(p)); args.push_back(std::move(k));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__storef32", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("memcpy")) {
            auto dst = expression(); auto src = expression(); auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(dst)); args.push_back(std::move(src)); args.push_back(std::move(n));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__memcpy", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("memset")) {
            auto dst = expression(); auto val = expression(); auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(dst)); args.push_back(std::move(val)); args.push_back(std::move(n));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__memset", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("ptrdiff")) {
            auto a = expression(); auto b = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_diff", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("realloc")) {
            auto p = expression(); auto newSize = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(newSize));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__realloc", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("blocksize")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__block_size", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("ptroffset")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_offset", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        if (matchWord("ptrblock")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_block", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                )
            );
        }
        // for each x in a do (BLOCK end | STATEMENT) end
        if (matchWord("for")) {
            matchWord("each");
            Token var = consume(TokenType::IDENTIFIER, "Expect loop variable name.");
            matchWord("in");
            auto iterable = expression();
            matchWord("do");
            std::vector<std::unique_ptr<Stmt>> block;
            while (!isAtEnd() && !isWordAhead("end")) { block.push_back(parseCommand()); }
            matchWord("end");
            std::unique_ptr<Stmt> body = std::make_unique<BlockStmt>(std::move(block));
            matchWord("end");
            return std::make_unique<ForEachStmt>(var, std::move(iterable), std::move(body));
        }
        // repeat N times do ... end  -> for each _ in range(1, N) do ... end
        if (matchWord("repeat")) {
            auto n = expression();
            matchWord("times");
            matchWord("do");
            std::vector<std::unique_ptr<Expr>> rargs;
            rargs.push_back(std::make_unique<Literal>(1.0));
            rargs.push_back(std::move(n));
            auto rangeCall = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__range", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(rargs)
            );
            std::vector<std::unique_ptr<Stmt>> block;
            while (!isAtEnd() && !isWordAhead("end")) { block.push_back(parseCommand()); }
            matchWord("end");
            Token dummy{ TokenType::IDENTIFIER, "_", {}, 0 };
            std::unique_ptr<Stmt> body = std::make_unique<BlockStmt>(std::move(block));
            return std::make_unique<ForEachStmt>(dummy, std::move(rangeCall), std::move(body));
        }
        if (matchWord("add")) {
            auto left = comparison();
            if (!match({ TokenType::AND })) { matchWord("and"); }
            auto right = comparison();
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Binary>(std::move(left), Token{ TokenType::PLUS, "+", {}, 0 }, std::move(right))
            );
        }
        if (matchWord("subtract")) {
            auto left = comparison();
            matchWord("from");
            auto right = comparison();
            // interpret: subtract A from B -> B - A
            return std::make_unique<ExpressionStmt>(
                std::make_unique<Binary>(std::move(right), Token{ TokenType::MINUS, "-", {}, 0 }, std::move(left))
            );
        }
        if (matchWord("let") || matchWord("make")) {
            // let NAME be VALUE
            if (!check(TokenType::IDENTIFIER)) throw error(peek(), "Expect name after 'let'.");
            Token name = advance();
            if (!matchWord("be")) { matchWord("equal"); matchWord("to"); }
            auto value = expression();
            return std::make_unique<LetStmt>(name, std::make_unique<PrimitiveType>(Token{ TokenType::F64, "f64", {}, 0 }), std::move(value));
        }
        return std::make_unique<ExpressionStmt>(expression());
    }

private:
    // Concise syntax parsing helpers
    std::unique_ptr<Stmt> parseConciseFunction() {
        // fn NAME(PARAMS) { BODY }
        Token name = consume(TokenType::IDENTIFIER, "Expect function name after 'fn'.");
        consume(TokenType::LEFT_PAREN, "Expect '(' after function name.");
        
        std::vector<Parameter> params;
        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                Token pname = consume(TokenType::IDENTIFIER, "Expect parameter name.");
                params.push_back(Parameter{ pname, std::make_unique<PrimitiveType>(Token{ TokenType::F64, "f64", {}, 0 }) });
            } while (match({ TokenType::COMMA }));
        }
        consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
        
        auto retType = std::make_unique<PrimitiveType>(Token{ TokenType::F64, "f64", {}, 0 });
        std::vector<std::unique_ptr<Stmt>> body = parseBlock();
        
        return std::make_unique<FunctionStmt>(name, std::move(params), std::move(retType), std::move(body));
    }
    
    std::unique_ptr<Stmt> parseConciseIf() {
        // if (CONDITION) { THEN } [else { ELSE }]
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
        auto condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");
        
        std::vector<std::unique_ptr<Stmt>> thenBlock = parseBlock();
        auto thenStmt = std::make_unique<BlockStmt>(std::move(thenBlock));
        
        std::unique_ptr<Stmt> elseStmt;
        if (matchWord("else")) {
            std::vector<std::unique_ptr<Stmt>> elseBlock = parseBlock();
            elseStmt = std::make_unique<BlockStmt>(std::move(elseBlock));
        }
        
        return std::make_unique<IfStmt>(std::move(condition), std::move(thenStmt), std::move(elseStmt));
    }
    
    std::unique_ptr<Stmt> parseConciseWhile() {
        // while (CONDITION) { BODY }
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
        auto condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after while condition.");
        
        std::vector<std::unique_ptr<Stmt>> bodyBlock = parseBlock();
        auto bodyStmt = std::make_unique<BlockStmt>(std::move(bodyBlock));
        
        return std::make_unique<WhileStmt>(std::move(condition), std::move(bodyStmt));
    }
    
    std::vector<std::unique_ptr<Stmt>> parseBlock() {
        // { STMT; STMT; ... }
        consume(TokenType::LEFT_BRACE, "Expect '{' to start block.");
        std::vector<std::unique_ptr<Stmt>> statements;
        
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            // Skip empty statements (just semicolons)
            if (match({TokenType::SEMICOLON})) {
                continue;
            }
            
            // Try to parse as statement; if that fails, try as expression
            try {
                statements.push_back(parseCommand());
            } catch (...) {
                // If statement parsing fails, try as expression statement
                try {
                    auto expr = expression();
                    statements.push_back(std::make_unique<ExpressionStmt>(std::move(expr)));
                } catch (...) {
                    // If both fail, skip this token and continue
                    if (!isAtEnd() && !check(TokenType::RIGHT_BRACE)) {
                        advance();
                    }
                    continue;
                }
            }
            
            // Optional semicolon after each statement
            match({TokenType::SEMICOLON});
        }
        
        consume(TokenType::RIGHT_BRACE, "Expect '}' to end block.");
        return statements;
    }

    // Parses a function header "function name with parameters a, b returning number:" after the initial 'define'
    // Outputs name, params, and return type. Returns true on success.
    bool parseFunctionHeader(Token& outName, std::vector<Parameter>& outParams, std::unique_ptr<Type>& outReturnType) {
        if (!matchWord("function")) return false;
        if (!check(TokenType::IDENTIFIER)) return false;
        outName = advance();
        matchWord("with"); matchWord("parameters");
        while (!check(TokenType::END_OF_FILE) && !isWordAhead("returning")) {
            if (check(TokenType::IDENTIFIER)) {
                Token pname = advance();
                outParams.push_back(Parameter{ pname, std::make_unique<PrimitiveType>(Token{ TokenType::F64, "f64", {}, 0 }) });
            }
            if (peek().lexeme == ",") advance();
            matchWord("and");
        }
        matchWord("returning");
        matchWord("number");
        if (check(TokenType::COLON)) advance();
        outReturnType = std::make_unique<PrimitiveType>(Token{ TokenType::F64, "f64", {}, 0 });
        return true;
    }
    struct ParseError : public std::runtime_error { using std::runtime_error::runtime_error; };

    std::unique_ptr<Expr> expression() { return equality(); }

    std::unique_ptr<Expr> equality() {
        auto expr = logic();
        for (;;) {
            if (match({ TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL })) {
                Token op = previous();
                auto right = logic();
                expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
                continue;
            }
            if (matchWord("is")) {
                Token op{ TokenType::EQUAL_EQUAL, "is", {}, 0 };
                if (matchWord("not")) { op = Token{ TokenType::BANG_EQUAL, "is not", {}, 0 }; }
                auto right = logic();
                expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
                continue;
            }
            break;
        }
        return expr;
    }

    std::unique_ptr<Expr> logic() {
        auto expr = comparison();
        while (match({ TokenType::AND, TokenType::OR })) {
            Token op = previous();
            auto right = comparison();
            // desugar to binary for now; interpreter will treat non-zero as truthy later via isTruthy around comparisons
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> comparison() {
        auto expr = term();
        while (match({ TokenType::GREATER, TokenType::GREATER_EQUAL, TokenType::LESS, TokenType::LESS_EQUAL })) {
            Token op = previous();
            auto right = term();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> term() {
        auto expr = factor();
        while (match({ TokenType::PLUS, TokenType::MINUS })) {
            Token op = previous();
            auto right = factor();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> factor() {
        auto expr = unary();
        while (match({ TokenType::ASTERISK, TokenType::SLASH, TokenType::MODULO })) {
            Token op = previous();
            auto right = unary();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> unary() {
        if (match({ TokenType::MINUS })) {
            Token op = previous();
            auto right = unary();
            return std::make_unique<Unary>(op, std::move(right));
        }
        return primary();
    }

    std::unique_ptr<Expr> primary() {
        std::unique_ptr<Expr> expr;
        if (match({ TokenType::NUMBER_LITERAL, TokenType::STRING_LITERAL })) {
            expr = std::make_unique<Literal>(previous().literal);
        } else if (match({ TokenType::LEFT_BRACKET })) {
            std::vector<std::unique_ptr<Expr>> elements;
            if (!check(TokenType::RIGHT_BRACKET)) {
                do { elements.push_back(expression()); } while (match({ TokenType::COMMA }));
            }
            consume(TokenType::RIGHT_BRACKET, "Expect ']' after array literal.");
            expr = std::make_unique<ArrayLiteral>(std::move(elements));
        } else if (matchWord("true")) {
            expr = std::make_unique<Literal>(true);
        } else if (matchWord("false")) {
            expr = std::make_unique<Literal>(false);
        } else if (matchWord("new")) {
            // new map / new dictionary / new dict
            if (matchWord("map") || matchWord("dictionary") || matchWord("dict")) {
                std::vector<std::unique_ptr<Expr>> noargs;
                expr = std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__new_map", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(noargs)
                );
            } else {
                throw error(peek(), "Unknown construction after 'new'.");
            }
        } else if (matchWord("get")) {
            // get KEY from MAP -> __map_get(MAP, KEY)
            auto key = expression();
            matchWord("from");
            auto map = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(map));
            args.push_back(std::move(key));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_get", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("has")) {
            // has KEY in MAP -> __map_has(MAP, KEY)
            auto key = expression();
            matchWord("in");
            auto map = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(map));
            args.push_back(std::move(key));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_has", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("does")) {
            // does MAP have KEY -> __map_has(MAP, KEY)
            auto map = expression();
            matchWord("have");
            auto key = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(map));
            args.push_back(std::move(key));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_has", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("char")) {
            // char at IDX in STR -> __str_index(STR, IDX)
            matchWord("at");
            auto idx = expression();
            matchWord("in");
            auto str = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(str));
            args.push_back(std::move(idx));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__str_index", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("substring")) {
            // substring of STR from A to B -> __substr(STR, A, B)
            matchWord("of");
            auto str = expression();
            matchWord("from");
            auto a = expression();
            matchWord("to");
            auto b = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(str));
            args.push_back(std::move(a));
            args.push_back(std::move(b));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__substr", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("ord")) {
            // ord of STR -> __ord(STR)
            if (!matchWord("of")) { matchWord("from"); }
            auto str = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(str));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ord", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("chr")) {
            // chr N -> __chr(N)
            auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(n));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__chr", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("read")) {
            // read file PATH -> __read_file(PATH)
            matchWord("file");
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__read_file", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("write")) {
            // write DATA to file PATH -> __write_file(PATH, DATA)
            auto data = expression();
            matchWord("to");
            matchWord("file");
            auto path = expression();
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(path));
            args.push_back(std::move(data));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__write_file", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("find")) {
            // find NEEDLE in HAYSTACK -> __str_find(HAYSTACK, NEEDLE)
            auto needle = expression();
            matchWord("in");
            auto hay = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(hay)); args.push_back(std::move(needle));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__str_find", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("split")) {
            // split STR by SEP -> __split(STR, SEP)
            auto str = expression();
            matchWord("by");
            auto sep = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(str)); args.push_back(std::move(sep));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__split", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("concat")) {
            // concat A and B -> __str_cat(A, B)
            auto a = expression(); if (!match({ TokenType::AND })) { matchWord("and"); } auto b = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__str_cat", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("pack64")) {
            // pack64 N -> __pack_f64le(N)
            auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(n));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__pack_f64le", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("pack16")) {
            auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(n));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__pack_u16le", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("pack32")) {
            auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(n));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__pack_u32le", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("assert")) {
            auto cond = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(cond));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__assert", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("panic")) {
            auto msg = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(msg));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__panic", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("length")) {
            matchWord("of");
            auto a = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__len", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("alloc")) {
            auto n = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(n));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__alloc", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("free")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__free", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("tostring")) {
            auto x = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(x));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__to_string", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("floor")) {
            auto x = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(x));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__floor", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("ceil")) {
            auto x = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(x));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ceil", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("round")) {
            auto x = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(x));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__round", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("sqrt")) {
            auto x = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(x));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__sqrt", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("abs")) {
            auto x = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(x));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__abs", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("pow")) {
            auto a = expression(); matchWord("by"); auto b = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__pow", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("band")) {
            auto a = comparison(); if (!match({ TokenType::AND })) { matchWord("and"); } auto b = comparison();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__band", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("bor")) {
            auto a = comparison(); if (!match({ TokenType::AND })) { matchWord("and"); } auto b = comparison();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__bor", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("bxor")) {
            auto a = comparison(); if (!match({ TokenType::AND })) { matchWord("and"); } auto b = comparison();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__bxor", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("shl")) {
            auto a = expression(); matchWord("by"); auto b = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__shl", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("shr")) {
            auto a = expression(); matchWord("by"); auto b = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__shr", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("size")) {
            matchWord("of"); auto m = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(m));
            expr = std::make_unique<Call>(std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_size", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args));
        } else if (matchWord("ptradd")) {
            auto p = expression(); matchWord("by"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_add", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("read8")) {
            auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__load8", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("write8")) {
            auto v = expression(); matchWord("to"); auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(v)); args.push_back(std::move(p)); args.push_back(std::move(k));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__store8", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("read32")) {
            auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(k));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__load32", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("write32")) {
            auto v = expression(); matchWord("to"); auto p = expression(); matchWord("at"); auto k = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(v)); args.push_back(std::move(p)); args.push_back(std::move(k));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__store32", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("blocksize")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__block_size", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("ptroffset")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_offset", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("ptrblock")) {
            auto p = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_block", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("ptrdiff")) {
            auto a = expression(); auto b = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_diff", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("realloc")) {
            auto p = expression(); auto newSize = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(newSize));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__realloc", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (matchWord("range")) {
            matchWord("from"); auto a = expression(); matchWord("to"); auto b = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b));
            expr = std::make_unique<Call>(
                std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__range", {}, 0 }),
                Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                std::move(args)
            );
        } else if (match({ TokenType::IDENTIFIER })) {
            Token ident = previous();
            if (match({ TokenType::LEFT_PAREN })) {
                std::vector<std::unique_ptr<Expr>> args;
                if (!check(TokenType::RIGHT_PAREN)) {
                    do { args.push_back(expression()); } while (match({ TokenType::COMMA }));
                }
                consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
                expr = std::make_unique<Call>(std::make_unique<Variable>(ident), ident, std::move(args));
            } else {
                expr = std::make_unique<Variable>(ident);
            }
        } else if (match({ TokenType::LEFT_PAREN })) {
            expr = expression();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
        } else {
            throw error(peek(), "Expect expression.");
        }
        // parse int/float
        if (matchWord("parse")) {
            if (matchWord("int")) {
                auto s = expression();
                std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(s));
                expr = std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__parse_int", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                );
            } else if (matchWord("float")) {
                auto s = expression();
                std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(s));
                expr = std::make_unique<Call>(
                    std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__parse_float", {}, 0 }),
                    Token{ TokenType::LEFT_PAREN, "(", {}, 0 },
                    std::move(args)
                );
            }
        }
        // starts with / ends with
        if (matchWord("starts")) { matchWord("with"); auto p = expression(); matchWord("in"); auto s = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(s)); args.push_back(std::move(p)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__starts_with", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        if (matchWord("ends")) { matchWord("with"); auto p = expression(); matchWord("in"); auto s = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(s)); args.push_back(std::move(p)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ends_with", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        // map delete/keys
        if (matchWord("delete")) { matchWord("key"); auto k = expression(); matchWord("from"); auto m = expression(); std::vector<std::unique_ptr<std::unique_ptr<Expr>>> __dummy; std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(m)); args.push_back(std::move(k)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_del", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        if (matchWord("keys")) { matchWord("of"); auto m = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(m)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__map_keys", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        // pointer helpers as expressions
        if (matchWord("blocksize")) { auto p = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__block_size", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        if (matchWord("ptroffset")) { auto p = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_offset", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        if (matchWord("ptrblock")) { auto p = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_block", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        if (matchWord("ptrdiff")) { auto a = expression(); auto b = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(a)); args.push_back(std::move(b)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__ptr_diff", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }
        // file exists
        if (matchWord("exists")) { matchWord("file"); auto p = expression(); std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__file_exists", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) ); }

        // realloc as expression: realloc P NEWSIZE -> __realloc(P, NEWSIZE)
        if (matchWord("realloc")) {
            auto p = expression(); auto newSize = expression();
            std::vector<std::unique_ptr<Expr>> args; args.push_back(std::move(p)); args.push_back(std::move(newSize));
            expr = std::make_unique<Call>( std::make_unique<Variable>(Token{ TokenType::IDENTIFIER, "__realloc", {}, 0 }), Token{ TokenType::LEFT_PAREN, "(", {}, 0 }, std::move(args) );
        }

        // Postfix indexing on any primary
        while (match({ TokenType::LEFT_BRACKET })) {
            auto idx = expression();
            consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");
            expr = std::make_unique<Index>(std::move(expr), previous(), std::move(idx));
        }
        return expr;
    }

    // Utilities
    bool matchWord(const std::string& word) {
        if (!check(TokenType::IDENTIFIER)) return false;
        if (peek().lexeme == word) { advance(); return true; }
        return false;
    }

    bool isWordAhead(const std::string& word) {
        if (!check(TokenType::IDENTIFIER)) return false;
        return peek().lexeme == word;
    }
    bool match(const std::vector<TokenType>& types) {
        for (TokenType type : types) {
            if (check(type)) { advance(); return true; }
        }
        return false;
    }
    Token consume(TokenType type, const std::string& message) {
        if (check(type)) return advance();
        throw error(peek(), message);
    }
    bool check(TokenType type) const { if (isAtEnd()) return false; return peek().type == type; }
    Token advance() { if (!isAtEnd()) current++; return previous(); }
    bool isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }
    Token peek() const { return tokens[current]; }
    Token peekNext() const { 
        if (current + 1 >= tokens.size()) {
            return Token{TokenType::END_OF_FILE, "", {}, 0, 0};
        }
        return tokens[current + 1]; 
    }
    Token previous() const { return tokens[current - 1]; }
    ParseError error(Token tok, const std::string& message) {
        std::string where;
        if (tok.type == TokenType::END_OF_FILE) {
            where = " at end of input";
        } else {
            if (tok.line > 0) where += std::string(" at line ") + std::to_string(tok.line);
            if (tok.col > 0) where += std::string(", col ") + std::to_string(tok.col);
            if (!tok.lexeme.empty()) where += std::string(", near '") + tok.lexeme + "'";
        }
        // Attach a small, static hint catalog for a few common mistakes
        std::string hint;
        if (message.find("] after index") != std::string::npos) hint = " Did you forget a closing ']'?";
        if (message.find("')' after arguments") != std::string::npos) hint = " Did you forget a closing ')' or a comma/and between arguments?";
        if (message.find("Expect expression") != std::string::npos) hint = " If using English forms, ensure keywords like 'and', 'by', 'to' are present.";
        return ParseError(message + where + hint);
    }

    const std::vector<Token>& tokens;
    int current = 0;
};


