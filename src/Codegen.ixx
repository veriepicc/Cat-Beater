export module Codegen;

import IR;
import Expr;
import Stmt;
import <memory>;
import <unordered_map>;
import <string>;
import <vector>;

export class Codegen : public ExprVisitor, public StmtVisitor {
public:
    Chunk chunk;
    bool inFunction = false;
    bool emittedReturnInFunction = false;
    // locals: stack of scopes; each scope is name -> slot index
    std::vector<std::unordered_map<std::string, std::uint16_t>> scopeStack;
    std::uint16_t nextLocalSlot = 0;
    // debug: per top-level statement starting line/col
    std::vector<std::uint32_t> stmtLines;
    std::vector<std::uint32_t> stmtCols;

    void setStmtLines(const std::vector<std::uint32_t>& lines) { stmtLines = lines; }

    void emit(const std::vector<std::unique_ptr<Stmt>>& program) {
        // Emit top-level statements; record simple functions as entry labels
        for (size_t si = 0; si < program.size(); ++si) {
            const auto& s = program[si];
            std::uint32_t line = (si < stmtLines.size() ? stmtLines[si] : 0);
            size_t startTop = chunk.code.size();
            if (auto fn = dynamic_cast<const FunctionStmt*>(s.get())) {
                auto ni = chunk.addName(fn->name.lexeme);
                // Insert a jump over the function body so top-level doesn't execute it
                size_t skipPos = chunk.code.size();
                chunk.writeOp(OpCode::OP_JUMP); chunk.writeU16(0);
                // entry is current code position (start of function body)
                std::uint32_t entryPos = static_cast<std::uint32_t>(chunk.code.size());
                chunk.functions.push_back({ ni, static_cast<std::uint16_t>(fn->params.size()), entryPos });
                // function context
                bool prevInFunction = inFunction; inFunction = true;
                emittedReturnInFunction = false;
                auto savedScopes = scopeStack; auto savedNext = nextLocalSlot;
                scopeStack.clear(); nextLocalSlot = 0;
                // map params to local slots 0..n-1
                scopeStack.emplace_back();
                for (std::uint16_t i = 0; i < fn->params.size(); ++i) {
                    scopeStack.back()[fn->params[i].name.lexeme] = i;
                }
                nextLocalSlot = static_cast<std::uint16_t>(fn->params.size());
                // Emit body; if last is expression, return its value
                for (size_t i = 0; i < fn->body.size(); ++i) {
                    const bool isLast = (i + 1 == fn->body.size());
                    if (isLast) {
                        if (auto exprStmt = dynamic_cast<const ExpressionStmt*>(fn->body[i].get())) {
                            // evaluate expression and return
                            exprStmt->expression->accept(*this);
                            chunk.writeOp(OpCode::OP_RETURN);
                            emittedReturnInFunction = true;
                            continue;
                        }
                    }
                    fn->body[i]->accept(*this);
                }
                if (!emittedReturnInFunction) {
                    // return nil
                    auto ci = chunk.addConstant(std::monostate{});
                    chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
                    chunk.writeOp(OpCode::OP_RETURN);
                }
                // Patch the skip jump to here (after function body)
                patchJump(skipPos);
                // restore context
                scopeStack = std::move(savedScopes);
                nextLocalSlot = savedNext;
                inFunction = prevInFunction;
                size_t endTop = chunk.code.size();
                if (line) {
                    if (chunk.debugLines.size() < endTop) chunk.debugLines.resize(endTop, 0);
                    if (chunk.debugCols.size() < endTop) chunk.debugCols.resize(endTop, 0);
                    std::uint32_t col = (si < stmtCols.size() ? stmtCols[si] : 1);
                    for (size_t p = startTop; p < endTop; ++p) { chunk.debugLines[p] = line; chunk.debugCols[p] = col; }
                }
            } else {
                s->accept(*this);
                size_t endTop = chunk.code.size();
                if (line) {
                    if (chunk.debugLines.size() < endTop) chunk.debugLines.resize(endTop, 0);
                    if (chunk.debugCols.size() < endTop) chunk.debugCols.resize(endTop, 0);
                    std::uint32_t col = (si < stmtCols.size() ? stmtCols[si] : 1);
                    for (size_t p = startTop; p < endTop; ++p) { chunk.debugLines[p] = line; chunk.debugCols[p] = col; }
                }
            }
        }
        chunk.writeOp(OpCode::OP_HALT);
        if (!stmtLines.empty() && chunk.debugLines.size() < chunk.code.size()) {
            chunk.debugLines.resize(chunk.code.size(), stmtLines.back());
            if (!stmtCols.empty()) chunk.debugCols.resize(chunk.code.size(), stmtCols.back());
        }
    }

    // Stmt
    std::any visit(const ExpressionStmt& stmt) override {
        // At top-level, echo expression results (like interpreter). Inside functions, don't echo.
        if (auto call = dynamic_cast<const Call*>(stmt.expression.get())) {
            if (auto var = dynamic_cast<const Variable*>(call->callee.get())) {
                const std::string& name = var->name.lexeme;
                if (name == "print") {
                    stmt.expression->accept(*this);
                    return {};
                }
                if (name == "__append" || name == "__alloc" || name == "__free" ||
                    name == "__ptr_add" || name == "__store8" || name == "__store32" ||
                    name == "__pop" || name == "__map_set") {
                    stmt.expression->accept(*this);
                    return {};
                }
            }
        }
        stmt.expression->accept(*this);
        if (!inFunction) {
            chunk.writeOp(OpCode::OP_PRINT);
            chunk.writeByte(1);
        }
        return {};
    }
    std::any visit(const LetStmt& stmt) override {
        stmt.initializer->accept(*this);
        if (inFunction) {
            if (scopeStack.empty()) scopeStack.emplace_back();
            auto& scope = scopeStack.back();
            std::uint16_t slot;
            auto it = scope.find(stmt.name.lexeme);
            if (it == scope.end()) { slot = nextLocalSlot++; scope[stmt.name.lexeme] = slot; }
            else slot = it->second;
            chunk.writeOp(OpCode::OP_SET_LOCAL);
            chunk.writeU16(slot);
        } else {
            auto ni = chunk.addName(stmt.name.lexeme);
            chunk.writeOp(OpCode::OP_SET_GLOBAL);
            chunk.writeU16(ni);
        }
        return {};
    }
    std::any visit(const FunctionStmt&) override { return {}; }
    std::any visit(const ReturnStmt& stmt) override {
        // evaluate value (or nil)
        if (stmt.value) {
            stmt.value->accept(*this);
        } else {
            auto ci = chunk.addConstant(std::monostate{});
            chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
        }
        chunk.writeOp(OpCode::OP_RETURN);
        emittedReturnInFunction = true;
        return {};
    }
    std::any visit(const BlockStmt& stmt) override {
        bool pushed = false;
        if (inFunction) { scopeStack.emplace_back(); pushed = true; }
        for (const auto& s : stmt.statements) s->accept(*this);
        if (pushed) scopeStack.pop_back();
        return {};
    }
    std::any visit(const SetStmt& stmt) override {
        stmt.value->accept(*this);
        if (inFunction) {
            for (int si = static_cast<int>(scopeStack.size()) - 1; si >= 0; --si) {
                auto& scope = scopeStack[si];
                auto it = scope.find(stmt.name.lexeme);
                if (it != scope.end()) {
                    chunk.writeOp(OpCode::OP_SET_LOCAL);
                    chunk.writeU16(it->second);
                    return {};
                }
            }
        }
        auto ni = chunk.addName(stmt.name.lexeme);
        chunk.writeOp(OpCode::OP_SET_GLOBAL);
        chunk.writeU16(ni);
        return {};
    }
    std::any visit(const IfStmt& stmt) override {
        // condition
        stmt.condition->accept(*this);
        // jump if false to else/start
        size_t jifPos = chunk.code.size();
        chunk.writeOp(OpCode::OP_JUMP_IF_FALSE); chunk.writeU16(0);
        // discard condition (true branch)
        chunk.writeOp(OpCode::OP_POP);
        // then branch
        stmt.thenBranch->accept(*this);
        // jump to end
        size_t jmpPos = chunk.code.size();
        chunk.writeOp(OpCode::OP_JUMP); chunk.writeU16(0);
        // patch jif to here
        patchJump(jifPos);
        // discard condition (false branch)
        chunk.writeOp(OpCode::OP_POP);
        // else if exists
        if (stmt.elseBranch) stmt.elseBranch->accept(*this);
        // patch end jump
        patchJump(jmpPos);
        return {};
    }
    std::any visit(const ForEachStmt&) override { return {}; }
    std::any visit(const WhileStmt& stmt) override {
        size_t loopStart = chunk.code.size();
        // condition
        stmt.condition->accept(*this);
        size_t jifPos = chunk.code.size();
        chunk.writeOp(OpCode::OP_JUMP_IF_FALSE); chunk.writeU16(0);
        // discard condition when true
        chunk.writeOp(OpCode::OP_POP);
        // body
        stmt.body->accept(*this);
        // loop back
        emitLoop(loopStart);
        // patch exit
        patchJump(jifPos);
        return {};
    }

    // Expr
    std::any visit(const Binary& expr) override {
        expr.left->accept(*this);
        expr.right->accept(*this);
        switch (expr.op.type) {
        case TokenType::PLUS: chunk.writeOp(OpCode::OP_ADD); break;
        case TokenType::MINUS: chunk.writeOp(OpCode::OP_SUB); break;
        case TokenType::ASTERISK: chunk.writeOp(OpCode::OP_MUL); break;
        case TokenType::SLASH: chunk.writeOp(OpCode::OP_DIV); break;
        case TokenType::MODULO: chunk.writeOp(OpCode::OP_MOD); break;
        case TokenType::GREATER: chunk.writeOp(OpCode::OP_GT); break;
        case TokenType::GREATER_EQUAL: chunk.writeOp(OpCode::OP_GE); break;
        case TokenType::LESS: chunk.writeOp(OpCode::OP_LT); break;
        case TokenType::LESS_EQUAL: chunk.writeOp(OpCode::OP_LE); break;
        case TokenType::EQUAL_EQUAL: chunk.writeOp(OpCode::OP_EQ); break;
        case TokenType::BANG_EQUAL: chunk.writeOp(OpCode::OP_NE); break;
        case TokenType::AND: chunk.writeOp(OpCode::OP_AND); break;
        case TokenType::OR: chunk.writeOp(OpCode::OP_OR); break;
        default: break;
        }
        return {};
    }
    std::any visit(const Grouping& expr) override { return expr.expression->accept(*this); }
    std::any visit(const Literal& expr) override {
        if (std::holds_alternative<double>(expr.value)) {
            auto ci = chunk.addConstant(std::get<double>(expr.value));
            chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
        } else if (std::holds_alternative<std::string>(expr.value)) {
            auto ci = chunk.addConstant(std::get<std::string>(expr.value));
            chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
        } else if (std::holds_alternative<bool>(expr.value)) {
            auto ci = chunk.addConstant(std::get<bool>(expr.value));
            chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
        } else {
            auto ci = chunk.addConstant(std::monostate{});
            chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
        }
        return {};
    }
    std::any visit(const Unary& expr) override {
        if (expr.op.type == TokenType::MINUS) {
            // Emit 0 - value to implement unary minus correctly
            auto ci = chunk.addConstant(0.0);
            chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
            expr.right->accept(*this);
            chunk.writeOp(OpCode::OP_SUB);
            return {};
        }
        // Fallback: just evaluate the operand
        expr.right->accept(*this);
        return {};
    }
    std::any visit(const Variable& expr) override {
        if (inFunction) {
            for (int si = static_cast<int>(scopeStack.size()) - 1; si >= 0; --si) {
                auto& scope = scopeStack[si];
                auto it = scope.find(expr.name.lexeme);
                if (it != scope.end()) {
                    chunk.writeOp(OpCode::OP_GET_LOCAL);
                    chunk.writeU16(it->second);
                    return {};
                }
            }
        }
        for (const auto& f : chunk.functions) {
            if (chunk.names[f.nameIndex] == expr.name.lexeme) {
                auto ci = chunk.addConstant(std::string(expr.name.lexeme));
                chunk.writeOp(OpCode::OP_CONST); chunk.writeU16(ci);
                return {};
            }
        }
        auto ni = chunk.addName(expr.name.lexeme);
        chunk.writeOp(OpCode::OP_GET_GLOBAL);
        chunk.writeU16(ni);
        return {};
    }
    std::any visit(const Assign& expr) override {
        expr.value->accept(*this);
        auto ni = chunk.addName(expr.name.lexeme);
        chunk.writeOp(OpCode::OP_SET_GLOBAL);
        chunk.writeU16(ni);
        return {};
    }
    std::any visit(const Call& expr) override {
        // Special-case print as VM print for now
        if (auto v = dynamic_cast<const Variable*>(expr.callee.get())) {
            if (v->name.lexeme == "print") {
                for (const auto& a : expr.arguments) a->accept(*this);
                chunk.writeOp(OpCode::OP_PRINT);
                chunk.writeByte(static_cast<std::uint8_t>(expr.arguments.size()));
                return {};
            }
            if (v->name.lexeme == "__len") {
                // one arg
                expr.arguments[0]->accept(*this);
                chunk.writeOp(OpCode::OP_LEN);
                return {};
            }
            if (v->name.lexeme == "__append") {
                // args: arr, val
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_APPEND);
                return {};
            }
            if (v->name.lexeme == "__pop") {
                expr.arguments[0]->accept(*this);
                chunk.writeOp(OpCode::OP_ARRAY_POP);
                return {};
            }
            // maps
            if (v->name.lexeme == "__new_map") { chunk.writeOp(OpCode::OP_NEW_MAP); return {}; }
            if (v->name.lexeme == "__map_get") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_MAP_GET); return {}; }
            if (v->name.lexeme == "__map_set") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                expr.arguments[2]->accept(*this);
                chunk.writeOp(OpCode::OP_MAP_SET); return {}; }
            if (v->name.lexeme == "__map_has") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_MAP_HAS); return {}; }
            // string helpers
            if (v->name.lexeme == "__str_index") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_STR_INDEX); return {}; }
            if (v->name.lexeme == "__substr") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                expr.arguments[2]->accept(*this);
                chunk.writeOp(OpCode::OP_SUBSTR); return {}; }
            if (v->name.lexeme == "__str_cat") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_STR_CAT); return {}; }
            if (v->name.lexeme == "__str_find") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_STR_FIND); return {}; }
            if (v->name.lexeme == "__split") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_SPLIT); return {}; }
            if (v->name.lexeme == "__ord") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ORD); return {}; }
            if (v->name.lexeme == "__chr") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_CHR); return {}; }
            if (v->name.lexeme == "__read_file") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_READ_FILE); return {}; }
            if (v->name.lexeme == "__write_file") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_WRITE_FILE); return {}; }
            if (v->name.lexeme == "__pack_f64le") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PACK_F64LE); return {}; }
            if (v->name.lexeme == "__pack_u16le") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PACK_U16LE); return {}; }
            if (v->name.lexeme == "__pack_u32le") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PACK_U32LE); return {}; }
            if (v->name.lexeme == "__assert") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ASSERT); return {}; }
            if (v->name.lexeme == "__panic") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PANIC); return {}; }
            if (v->name.lexeme == "__alloc") {
                expr.arguments[0]->accept(*this);
                chunk.writeOp(OpCode::OP_ALLOC);
                return {};
            }
            if (v->name.lexeme == "__join") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_JOIN);
                return {};
            }
            // rich strings
            if (v->name.lexeme == "__upper" || v->name.lexeme == "upper") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_STR_UPPER); return {}; }
            if (v->name.lexeme == "__lower" || v->name.lexeme == "lower") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_STR_LOWER); return {}; }
            if (v->name.lexeme == "__contains" || v->name.lexeme == "contains") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_STR_CONTAINS); return {}; }
            if (v->name.lexeme == "__format" || v->name.lexeme == "format") {
                // push fmt + args; encode argc as immediate
                for (const auto& a : expr.arguments) a->accept(*this);
                chunk.writeOp(OpCode::OP_FORMAT);
                chunk.writeByte(static_cast<std::uint8_t>(expr.arguments.size() - 1));
                return {};
            }
            if (v->name.lexeme == "__trim") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_TRIM); return {}; }
            if (v->name.lexeme == "__replace") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); expr.arguments[2]->accept(*this); chunk.writeOp(OpCode::OP_REPLACE); return {}; }
            if (v->name.lexeme == "__parse_int") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PARSE_INT); return {}; }
            if (v->name.lexeme == "__parse_float") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PARSE_FLOAT); return {}; }
            if (v->name.lexeme == "__starts_with") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_STARTS_WITH); return {}; }
            if (v->name.lexeme == "__ends_with") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_ENDS_WITH); return {}; }
            if (v->name.lexeme == "__map_del") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_MAP_DEL); return {}; }
            if (v->name.lexeme == "__map_keys") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_MAP_KEYS); return {}; }
            if (v->name.lexeme == "__file_exists") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_FILE_EXISTS); return {}; }
            if (v->name.lexeme == "__to_string") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_TO_STRING); return {}; }
            // streams
            if (v->name.lexeme == "__fopen" || v->name.lexeme == "fopen") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_FOPEN); return {}; }
            if (v->name.lexeme == "__fclose" || v->name.lexeme == "fclose") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_FCLOSE); return {}; }
            if (v->name.lexeme == "__fread" || v->name.lexeme == "fread") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_FREAD); return {}; }
            if (v->name.lexeme == "__freadline" || v->name.lexeme == "freadline") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_FREADLINE); return {}; }
            if (v->name.lexeme == "__fwrite" || v->name.lexeme == "fwrite") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_FWRITE); return {}; }
            if (v->name.lexeme == "__stdin" || v->name.lexeme == "stdin") { chunk.writeOp(OpCode::OP_STDIN); return {}; }
            if (v->name.lexeme == "__stdout" || v->name.lexeme == "stdout") { chunk.writeOp(OpCode::OP_STDOUT); return {}; }
            if (v->name.lexeme == "__stderr" || v->name.lexeme == "stderr") { chunk.writeOp(OpCode::OP_STDERR); return {}; }
            if (v->name.lexeme == "__emit_chunk" || v->name.lexeme == "emit_chunk") {
                // args: manifest(map), path(string)
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_EMIT_CHUNK);
                return {};
            }
            if (v->name.lexeme == "__ffi_call") {
                // args: dllName, funcName, ...args
                if (expr.arguments.size() < 2) { return {}; }
                // Push call args first, then dllName, then funcName so VM can pop func then dll
                if (expr.arguments.size() > 2) {
                    for (size_t i = 2; i < expr.arguments.size(); ++i) expr.arguments[i]->accept(*this);
                }
                expr.arguments[0]->accept(*this); // dll
                expr.arguments[1]->accept(*this); // func
                chunk.writeOp(OpCode::OP_FFI_CALL);
                chunk.writeByte(static_cast<std::uint8_t>(expr.arguments.size() - 2));
                return {};
            }
            if (v->name.lexeme == "__ffi_call_sig") {
                // args: dllName, funcName, signature, ...args
                if (expr.arguments.size() < 3) { return {}; }
                // Push call args first, then dll, then func, then signature so VM pops sig->func->dll
                if (expr.arguments.size() > 3) {
                    for (size_t i = 3; i < expr.arguments.size(); ++i) expr.arguments[i]->accept(*this);
                }
                expr.arguments[0]->accept(*this); // dll
                expr.arguments[1]->accept(*this); // func
                expr.arguments[2]->accept(*this); // signature
                chunk.writeOp(OpCode::OP_FFI_CALL_SIG);
                chunk.writeByte(static_cast<std::uint8_t>(expr.arguments.size() - 3));
                return {};
            }
            if (v->name.lexeme == "__ffi_proc") {
                // args: dllName, funcNameOrOrdinal
                if (expr.arguments.size() != 2) { return {}; }
                expr.arguments[0]->accept(*this); // dll
                expr.arguments[1]->accept(*this); // func or ordinal
                chunk.writeOp(OpCode::OP_FFI_PROC);
                return {};
            }
            if (v->name.lexeme == "__ffi_call_ptr") {
                // args: signature, ptr, ...args
                if (expr.arguments.size() < 2) { return {}; }
                if (expr.arguments.size() > 2) {
                    for (size_t i = 2; i < expr.arguments.size(); ++i) expr.arguments[i]->accept(*this);
                }
                expr.arguments[1]->accept(*this); // ptr as number
                expr.arguments[0]->accept(*this); // signature
                chunk.writeOp(OpCode::OP_FFI_CALL_PTR);
                chunk.writeByte(static_cast<std::uint8_t>(expr.arguments.size() - 2));
                return {};
            }
            if (v->name.lexeme == "__opcode_id" || v->name.lexeme == "opcode_id") {
                // one arg: name string
                expr.arguments[0]->accept(*this);
                chunk.writeOp(OpCode::OP_OPCODE_ID);
                return {};
            }
            if (v->name.lexeme == "__call_name_arr") {
                // args: funcName(string), argsArray(array)
                // push argsArray, then name to match OP_CALLN_ARR expectation
                expr.arguments[1]->accept(*this);
                expr.arguments[0]->accept(*this);
                chunk.writeOp(OpCode::OP_CALLN_ARR);
                return {};
            }
            if (v->name.lexeme == "__floor") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_FLOOR); return {}; }
            if (v->name.lexeme == "__ceil") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_CEIL); return {}; }
            if (v->name.lexeme == "__round") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ROUND); return {}; }
            if (v->name.lexeme == "__sqrt") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_SQRT); return {}; }
            if (v->name.lexeme == "__abs") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ABS); return {}; }
            if (v->name.lexeme == "__pow") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_POW); return {}; }
            if (v->name.lexeme == "__exp") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_EXP); return {}; }
            if (v->name.lexeme == "__log") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_LOG); return {}; }
            if (v->name.lexeme == "__sin") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_SIN); return {}; }
            if (v->name.lexeme == "__cos") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_COS); return {}; }
            if (v->name.lexeme == "__tan") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_TAN); return {}; }
            if (v->name.lexeme == "__asin") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ASIN); return {}; }
            if (v->name.lexeme == "__acos") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ACOS); return {}; }
            if (v->name.lexeme == "__atan") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ATAN); return {}; }
            if (v->name.lexeme == "__atan2") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_ATAN2); return {}; }
            if (v->name.lexeme == "__random") { chunk.writeOp(OpCode::OP_RANDOM); return {}; }
            if (v->name.lexeme == "__band") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_BAND); return {}; }
            if (v->name.lexeme == "__bor") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_BOR); return {}; }
            if (v->name.lexeme == "__bxor") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_BXOR); return {}; }
            if (v->name.lexeme == "__shl") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_SHL); return {}; }
            if (v->name.lexeme == "__shr") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_SHR); return {}; }
            if (v->name.lexeme == "__array_reserve") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_ARRAY_RESERVE); return {}; }
            if (v->name.lexeme == "__array_clear") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_ARRAY_CLEAR); return {}; }
            if (v->name.lexeme == "__map_size") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_MAP_SIZE); return {}; }
            if (v->name.lexeme == "__map_clear") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_MAP_CLEAR); return {}; }
            if (v->name.lexeme == "__exit") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_EXIT); return {}; }
            if (v->name.lexeme == "__free") {
                expr.arguments[0]->accept(*this);
                chunk.writeOp(OpCode::OP_FREE);
                return {};
            }
            if (v->name.lexeme == "__ptr_add") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_PTR_ADD);
                return {};
            }
            if (v->name.lexeme == "__load8") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_LOAD8);
                return {};
            }
            if (v->name.lexeme == "__load16") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_LOAD16); return {}; }
            if (v->name.lexeme == "__store16") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); expr.arguments[2]->accept(*this); chunk.writeOp(OpCode::OP_STORE16); return {}; }
            if (v->name.lexeme == "__load64") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_LOAD64); return {}; }
            if (v->name.lexeme == "__store64") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); expr.arguments[2]->accept(*this); chunk.writeOp(OpCode::OP_STORE64); return {}; }
            if (v->name.lexeme == "__loadf32") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_LOADF32); return {}; }
            if (v->name.lexeme == "__storef32") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); expr.arguments[2]->accept(*this); chunk.writeOp(OpCode::OP_STOREF32); return {}; }
            if (v->name.lexeme == "__memcpy") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); expr.arguments[2]->accept(*this); chunk.writeOp(OpCode::OP_MEMCPY); return {}; }
            if (v->name.lexeme == "__memset") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); expr.arguments[2]->accept(*this); chunk.writeOp(OpCode::OP_MEMSET); return {}; }
            if (v->name.lexeme == "__ptr_diff") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_PTR_DIFF); return {}; }
            if (v->name.lexeme == "__realloc") { expr.arguments[0]->accept(*this); expr.arguments[1]->accept(*this); chunk.writeOp(OpCode::OP_REALLOC); return {}; }
            if (v->name.lexeme == "__block_size") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_BLOCK_SIZE); return {}; }
            if (v->name.lexeme == "__ptr_offset") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PTR_OFFSET); return {}; }
            if (v->name.lexeme == "__ptr_block") { expr.arguments[0]->accept(*this); chunk.writeOp(OpCode::OP_PTR_BLOCK); return {}; }
            if (v->name.lexeme == "__store8") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                expr.arguments[2]->accept(*this);
                chunk.writeOp(OpCode::OP_STORE8);
                return {};
            }
            if (v->name.lexeme == "__load32") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                chunk.writeOp(OpCode::OP_LOAD32);
                return {};
            }
            if (v->name.lexeme == "__store32") {
                expr.arguments[0]->accept(*this);
                expr.arguments[1]->accept(*this);
                expr.arguments[2]->accept(*this);
                chunk.writeOp(OpCode::OP_STORE32);
                return {};
            }
            // user function call by name
            for (const auto& a : expr.arguments) a->accept(*this);
            auto ni = chunk.addName(v->name.lexeme);
            chunk.writeOp(OpCode::OP_CALL);
            chunk.writeU16(ni);
            chunk.writeByte(static_cast<std::uint8_t>(expr.arguments.size()));
            return {};
        }
        return {};
    }
    std::any visit(const ArrayLiteral& expr) override {
        for (const auto& e : expr.elements) e->accept(*this);
        chunk.writeOp(OpCode::OP_NEW_ARRAY);
        chunk.writeByte(static_cast<std::uint8_t>(expr.elements.size()));
        return {};
    }
    std::any visit(const Index& expr) override {
        expr.array->accept(*this);
        expr.index->accept(*this);
        chunk.writeOp(OpCode::OP_INDEX_GET);
        return {};
    }
    std::any visit(const SetIndexStmt& stmt) override {
        // arr[idx] = value
        stmt.arrayExpr->accept(*this);
        stmt.indexExpr->accept(*this);
        stmt.value->accept(*this);
        chunk.writeOp(OpCode::OP_INDEX_SET);
        return {};
    }
private:
    void patchJump(size_t jumpPos) {
        // jumpPos points to opcode; next two bytes are placeholder 0,0
        size_t target = chunk.code.size();
        size_t offset = target - (jumpPos + 1 + 2);
        chunk.code[jumpPos + 1] = static_cast<std::uint8_t>(offset & 0xFF);
        chunk.code[jumpPos + 2] = static_cast<std::uint8_t>((offset >> 8) & 0xFF);
    }
    void emitLoop(size_t loopStart) {
        size_t from = chunk.code.size() + 1 + 2; // after writing op and u16
        size_t offset = from - loopStart;
        chunk.writeOp(OpCode::OP_LOOP);
        chunk.writeU16(static_cast<std::uint16_t>(offset));
    }
};


