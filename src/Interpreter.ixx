export module Interpreter;

import Expr;
import Stmt;
import <any>;
import <vector>;
import <memory>;
import <iostream>;
import <sstream>;
import <cstdint>;
import <algorithm>;
import <cmath>;
import Environment;

import Return;
import Function;
import Callable;

export class Interpreter : public ExprVisitor, public StmtVisitor {
public:
    std::shared_ptr<Environment> globals = std::make_shared<Environment>();
    std::shared_ptr<Environment> environment = globals;
    bool echoExpressions = false;
    bool debugLogging = false;

public:
    Interpreter() {
        class Print : public Callable {
        public:
            int arity() override { return 0; }
            bool variadic() override { return true; }

            std::any call(Interpreter& interpreter, const std::vector<std::any>& arguments) override {
                for (size_t idx = 0; idx < arguments.size(); ++idx) {
                    const auto& arg = arguments[idx];
                    interpreter.writeValue(arg);
                    if (idx + 1 < arguments.size()) std::cout << " ";
                }
                std::cout << std::endl;
                return nullptr;
            }

            std::string toString() override { return "<native fn>"; }
        };

        std::shared_ptr<Callable> printCallable = std::make_shared<Print>();
        globals->define("print", printCallable);
    }
    void interpret(const std::vector<std::unique_ptr<Stmt>>& statements) {
        try {
            for (const auto& statement : statements) {
                if (debugLogging) { std::cerr << "[execute] " << stmtTypeName(statement.get()) << std::endl; }
                execute(statement.get());
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Runtime Error: " << e.what() << std::endl;
        }
    }

public:
    void execute(Stmt* stmt) {
        stmt->accept(*this);
    }

    std::any evaluate(Expr* expr) {
        return expr->accept(*this);
    }
    
    void executeBlock(const std::vector<std::unique_ptr<Stmt>>& statements, std::shared_ptr<Environment> environment) {
        std::shared_ptr<Environment> previous = this->environment;
        this->environment = environment;

        for (const auto& statement : statements) {
            execute(statement.get());
        }

        this->environment = previous;
    }

        std::any visit(const ExpressionStmt& stmt) override {
        // Debug print removed
        if (debugLogging) { std::cerr << "[stmt] ExpressionStmt" << std::endl; }
        std::any value = evaluate(stmt.expression.get());
        if (echoExpressions && value.has_value() && value.type() != typeid(std::nullptr_t)) {
            writeValue(value);
            std::cout << std::endl;
        }
        if (debugLogging) { std::cerr << "[result] " << anyToDebugString(value) << std::endl; }
        return {};
    }

    std::any visit(const LetStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] LetStmt name=" << stmt.name.lexeme << std::endl; }
        std::any value = evaluate(stmt.initializer.get());
        if (debugLogging) { std::cerr << "[define] " << stmt.name.lexeme << " = " << anyToDebugString(value) << std::endl; }
        environment->define(stmt.name.lexeme, value);
        return {};
    }

    std::any visit(const FunctionStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] FunctionStmt name=" << stmt.name.lexeme << " params=" << stmt.params.size() << std::endl; }
        auto function = std::make_shared<Function>(&stmt, environment);
        environment->define(stmt.name.lexeme, std::static_pointer_cast<Callable>(function));
        return {};
    }


    std::any visit(const ReturnStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] ReturnStmt" << std::endl; }
        std::any value = nullptr;
        if (stmt.value != nullptr) {
            value = evaluate(stmt.value.get());
        }

        throw Return(value);
    }

    std::any visit(const BlockStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] BlockStmt enter (" << stmt.statements.size() << " stmts)" << std::endl; }
        executeBlock(stmt.statements, std::make_shared<Environment>(environment));
        if (debugLogging) { std::cerr << "[stmt] BlockStmt exit" << std::endl; }
        return {};
    }

    std::any visit(const SetStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] SetStmt name=" << stmt.name.lexeme << std::endl; }
        std::any value = evaluate(stmt.value.get());
        if (debugLogging) { std::cerr << "[assign] " << stmt.name.lexeme << " = " << anyToDebugString(value) << std::endl; }
        environment->assign(stmt.name.lexeme, value);
        return {};
    }

    std::any visit(const SetIndexStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] SetIndexStmt" << std::endl; }
        std::any arrAny = evaluate(stmt.arrayExpr.get());
        std::any idxAny = evaluate(stmt.indexExpr.get());
        std::any valAny = evaluate(stmt.value.get());
        if (arrAny.type() != typeid(std::shared_ptr<std::vector<std::any>>)) {
            throw std::runtime_error("Indexing non-array.");
        }
        if (idxAny.type() != typeid(double)) {
            throw std::runtime_error("Array index must be a number.");
        }
        auto vecPtr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(arrAny);
        int i = static_cast<int>(std::any_cast<double>(idxAny));
        if (i < 0 || i >= static_cast<int>(vecPtr->size())) {
            throw std::runtime_error("Array index out of bounds.");
        }
        (*vecPtr)[i] = valAny;
        return {};
    }

    std::any visit(const IfStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] IfStmt" << std::endl; }
        std::any condition = evaluate(stmt.condition.get());
        if (isTruthy(condition)) {
            execute(stmt.thenBranch.get());
        } else if (stmt.elseBranch != nullptr) {
            execute(stmt.elseBranch.get());
        }
        return {};
    }

    std::any visit(const WhileStmt& stmt) override {
        if (debugLogging) { std::cerr << "[stmt] WhileStmt enter" << std::endl; }
        int iter = 0;
        while (isTruthy(evaluate(stmt.condition.get()))) {
            if (debugLogging) { std::cerr << "[while] iter=" << iter++ << std::endl; }
            execute(stmt.body.get());
        }
        if (debugLogging) { std::cerr << "[stmt] WhileStmt exit" << std::endl; }
        return {};
    }

    std::any visit(const ForEachStmt& stmt) override {
        std::any iterAny = evaluate(stmt.iterable.get());
        if (iterAny.type() != typeid(std::shared_ptr<std::vector<std::any>>)) {
            throw std::runtime_error("for each expects an array.");
        }
        auto vec = std::any_cast<std::shared_ptr<std::vector<std::any>>>(iterAny);
        for (const auto& item : *vec) {
            environment->define(stmt.varName.lexeme, item);
            execute(stmt.body.get());
        }
        return {};
    }
    
    bool isTruthy(const std::any& object) {
        if (object.type() == typeid(nullptr)) return false;
        if (object.type() == typeid(bool)) return std::any_cast<bool>(object);
        if (object.type() == typeid(double)) return std::any_cast<double>(object) != 0.0;
        return true;
    }



    std::any visit(const Binary& expr) override {
        if (debugLogging) { std::cerr << "[expr] Binary op=" << (int)expr.op.type << std::endl; }
        std::any left = evaluate(expr.left.get());
        std::any right = evaluate(expr.right.get());

        switch (expr.op.type) {
        case TokenType::PLUS:
            return std::any_cast<double>(left) + std::any_cast<double>(right);
        case TokenType::MINUS:
            return std::any_cast<double>(left) - std::any_cast<double>(right);
        case TokenType::SLASH:
            return std::any_cast<double>(left) / std::any_cast<double>(right);
        case TokenType::ASTERISK:
            return std::any_cast<double>(left) * std::any_cast<double>(right);
        case TokenType::MODULO: {
            double l = std::any_cast<double>(left);
            double r = std::any_cast<double>(right);
            return std::fmod(l, r);
        }
        case TokenType::GREATER:
            return std::any_cast<double>(left) > std::any_cast<double>(right);
        case TokenType::GREATER_EQUAL:
            return std::any_cast<double>(left) >= std::any_cast<double>(right);
        case TokenType::LESS:
            return std::any_cast<double>(left) < std::any_cast<double>(right);
        case TokenType::LESS_EQUAL:
            return std::any_cast<double>(left) <= std::any_cast<double>(right);
        case TokenType::EQUAL_EQUAL: {
            if (left.type() == typeid(std::nullptr_t) && right.type() == typeid(std::nullptr_t)) return true;
            if (left.type() == typeid(double) && right.type() == typeid(double)) return std::any_cast<double>(left) == std::any_cast<double>(right);
            if (left.type() == typeid(std::string) && right.type() == typeid(std::string)) return std::any_cast<std::string>(left) == std::any_cast<std::string>(right);
            if (left.type() == typeid(bool) && right.type() == typeid(bool)) return std::any_cast<bool>(left) == std::any_cast<bool>(right);
            return false;
        }
        case TokenType::BANG_EQUAL: {
            if (left.type() == typeid(std::nullptr_t) && right.type() == typeid(std::nullptr_t)) return false;
            if (left.type() == typeid(double) && right.type() == typeid(double)) return std::any_cast<double>(left) != std::any_cast<double>(right);
            if (left.type() == typeid(std::string) && right.type() == typeid(std::string)) return std::any_cast<std::string>(left) != std::any_cast<std::string>(right);
            if (left.type() == typeid(bool) && right.type() == typeid(bool)) return std::any_cast<bool>(left) != std::any_cast<bool>(right);
            return true;
        }
        case TokenType::AND: {
            bool lb = isTruthy(left);
            if (!lb) return false;
            return isTruthy(right);
        }
        case TokenType::OR: {
            bool lb = isTruthy(left);
            if (lb) return true;
            return isTruthy(right);
        }
        }

        return {};
    }

    std::any visit(const Grouping& expr) override {
        if (debugLogging) { std::cerr << "[expr] Grouping" << std::endl; }
        return evaluate(expr.expression.get());
    }

    std::any visit(const Literal& expr) override {
        if (debugLogging) { std::cerr << "[expr] Literal" << std::endl; }
        if (std::holds_alternative<double>(expr.value)) {
            return std::get<double>(expr.value);
        }
        if (std::holds_alternative<std::string>(expr.value)) {
            return std::get<std::string>(expr.value);
        }
        if (std::holds_alternative<bool>(expr.value)) {
            return std::get<bool>(expr.value);
        }
        return nullptr; // nil
    }

    std::any visit(const ArrayLiteral& expr) override {
        auto values = std::make_shared<std::vector<std::any>>();
        values->reserve(expr.elements.size());
        for (const auto& el : expr.elements) {
            values->push_back(evaluate(el.get()));
        }
        return values; // store arrays by shared_ptr to avoid copying and support mutation later
    }

    std::any visit(const Index& expr) override {
        if (debugLogging) { std::cerr << "[expr] Index" << std::endl; }
        std::any arrAny = evaluate(expr.array.get());
        std::any idxAny = evaluate(expr.index.get());
        if (arrAny.type() != typeid(std::shared_ptr<std::vector<std::any>>)) {
            throw std::runtime_error("Indexing non-array.");
        }
        if (idxAny.type() != typeid(double)) {
            throw std::runtime_error("Array index must be a number.");
        }
        auto vecPtr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(arrAny);
        int i = static_cast<int>(std::any_cast<double>(idxAny));
        if (i < 0 || i >= static_cast<int>(vecPtr->size())) {
            throw std::runtime_error("Array index out of bounds.");
        }
        return (*vecPtr)[i];
    }

    std::any visit(const Unary& expr) override {
        if (debugLogging) { std::cerr << "[expr] Unary op=" << (int)expr.op.type << std::endl; }
        std::any right = evaluate(expr.right.get());

        switch (expr.op.type) {
        case TokenType::MINUS:
            return -std::any_cast<double>(right);
        }

        return {};
    }

    std::any visit(const Variable& expr) override {
        if (debugLogging) { std::cerr << "[expr] Variable get " << expr.name.lexeme << std::endl; }
        return environment->get(expr.name.lexeme);
    }

    std::any visit(const Assign& expr) override {
        if (debugLogging) { std::cerr << "[expr] Assign " << expr.name.lexeme << std::endl; }
        std::any value = evaluate(expr.value.get());
        environment->assign(expr.name.lexeme, value);
        return value;
    }

    std::any visit(const Call& expr) override {
        if (debugLogging) { std::cerr << "[expr] Call with " << expr.arguments.size() << " args" << std::endl; }
        // Builtins by variable name (check before evaluating callee to avoid undefined-variable errors)
        if (auto varCallee = dynamic_cast<const Variable*>(expr.callee.get())) {
            const std::string& builtin = varCallee->name.lexeme;
            if (builtin == "__len" || builtin == "__append" ||
                builtin == "__alloc" || builtin == "__free" || builtin == "__ptr_add" ||
                builtin == "__load8" || builtin == "__store8" || builtin == "__load32" || builtin == "__store32") {
                std::vector<std::any> args;
                args.reserve(expr.arguments.size());
                for (const auto& arg : expr.arguments) args.push_back(evaluate(arg.get()));
                return callBuiltin(builtin, args);
            }
        }

        std::any callee = evaluate(expr.callee.get());

        std::vector<std::any> arguments;
        for (const auto& arg : expr.arguments) {
            arguments.push_back(evaluate(arg.get()));
        }

        if (callee.type() == typeid(std::shared_ptr<Callable>)) {
            auto callable = std::any_cast<std::shared_ptr<Callable>>(callee);
            if (debugLogging) { std::cerr << "[call] user fn" << std::endl; }
            return callable->call(*this, arguments);
        }

        throw std::runtime_error("Can only call functions and classes.");
    }

    // Builtins dispatch
    std::any callBuiltin(const std::string& name, const std::vector<std::any>& args) {
        if (name == "__len") {
            if (args.size() != 1) throw std::runtime_error("len expects 1 argument.");
            if (args[0].type() != typeid(std::shared_ptr<std::vector<std::any>>)) throw std::runtime_error("len expects array.");
            auto vec = std::any_cast<std::shared_ptr<std::vector<std::any>>>(args[0]);
            return static_cast<double>(vec->size());
        }
        if (name == "__append") {
            if (args.size() != 2) throw std::runtime_error("append expects 2 arguments.");
            if (args[0].type() != typeid(std::shared_ptr<std::vector<std::any>>)) throw std::runtime_error("append expects array as first argument.");
            auto vec = std::any_cast<std::shared_ptr<std::vector<std::any>>>(args[0]);
            vec->push_back(args[1]);
            return nullptr;
        }
        if (name == "__pop") {
            if (args.size() != 1) throw std::runtime_error("pop expects 1 argument.");
            if (args[0].type() != typeid(std::shared_ptr<std::vector<std::any>>)) throw std::runtime_error("pop expects array.");
            auto vec = std::any_cast<std::shared_ptr<std::vector<std::any>>>(args[0]);
            if (vec->empty()) return nullptr;
            vec->pop_back();
            return nullptr;
        }
        if (name == "__range") {
            if (args.size() != 2) throw std::runtime_error("range expects 2 numbers (start, end inclusive).");
            if (args[0].type() != typeid(double) || args[1].type() != typeid(double)) throw std::runtime_error("range expects numbers.");
            int64_t a = static_cast<int64_t>(std::any_cast<double>(args[0]));
            int64_t b = static_cast<int64_t>(std::any_cast<double>(args[1]));
            auto out = std::make_shared<std::vector<std::any>>();
            if (a <= b) {
                out->reserve(static_cast<size_t>(b - a + 1));
                for (int64_t v = a; v <= b; ++v) out->push_back(static_cast<double>(v));
            } else {
                out->reserve(static_cast<size_t>(a - b + 1));
                for (int64_t v = a; v >= b; --v) out->push_back(static_cast<double>(v));
            }
            return out;
        }
        // Memory builtins
        if (name == "__alloc") {
            if (args.size() != 1) throw std::runtime_error("alloc expects 1 argument (size).");
            size_t n = static_cast<size_t>(asIndex(args[0], "size"));
            return makePointer(n);
        }
        if (name == "__free") {
            if (args.size() != 1) throw std::runtime_error("free expects 1 pointer.");
            auto p = asPointer(args[0]);
            freePointer(p);
            return nullptr;
        }
        if (name == "__ptr_add") {
            if (args.size() != 2) throw std::runtime_error("ptradd expects 2 arguments (ptr, offset).");
            auto p = asPointer(args[0]);
            int64_t off = asIndex(args[1], "offset");
            return offsetPointer(p, off);
        }
        if (name == "__load8") {
            if (args.size() != 2) throw std::runtime_error("read8 expects 2 arguments (ptr, offset).");
            auto p = asPointer(args[0]);
            int64_t off = asIndex(args[1], "offset");
            return static_cast<double>(load8(p, off));
        }
        if (name == "__store8") {
            if (args.size() != 3) throw std::runtime_error("write8 expects 3 arguments (value, ptr, offset).");
            uint8_t v = static_cast<uint8_t>(clampByte(asIndex(args[0], "value")));
            auto p = asPointer(args[1]);
            int64_t off = asIndex(args[2], "offset");
            store8(p, off, v);
            return nullptr;
        }
        if (name == "__load32") {
            if (args.size() != 2) throw std::runtime_error("read32 expects 2 arguments (ptr, offset).");
            auto p = asPointer(args[0]);
            int64_t off = asIndex(args[1], "offset");
            uint32_t val = load32(p, off);
            return static_cast<double>(val);
        }
        if (name == "__store32") {
            if (args.size() != 3) throw std::runtime_error("write32 expects 3 arguments (value, ptr, offset).");
            uint32_t v = static_cast<uint32_t>(asIndex(args[0], "value"));
            auto p = asPointer(args[1]);
            int64_t off = asIndex(args[2], "offset");
            store32(p, off, v);
            return nullptr;
        }
        throw std::runtime_error("Unknown builtin: " + name);
    }

    void writeValue(const std::any& value) {
        if (!value.has_value()) {
            std::cout << "nil";
        } else if (value.type() == typeid(std::nullptr_t)) {
            std::cout << "nil";
        } else if (value.type() == typeid(bool)) {
            std::cout << (std::any_cast<bool>(value) ? "true" : "false");
        } else if (value.type() == typeid(double)) {
            double d = std::any_cast<double>(value);
            // Print integers without scientific notation when representable exactly
            double truncated = std::trunc(d);
            if (std::fabs(d - truncated) < 1e-12 && truncated >= static_cast<double>(std::numeric_limits<int64_t>::min()) && truncated <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
                std::cout << static_cast<int64_t>(truncated);
            } else {
                std::cout << d;
            }
        } else if (value.type() == typeid(std::string)) {
            std::cout << std::any_cast<std::string>(value);
        } else if (value.type() == typeid(std::shared_ptr<std::vector<std::any>>)) {
            auto vec = std::any_cast<std::shared_ptr<std::vector<std::any>>>(value);
            std::cout << "[";
            for (size_t i = 0; i < vec->size(); ++i) {
                writeValue((*vec)[i]);
                if (i + 1 < vec->size()) std::cout << ", ";
            }
            std::cout << "]";
        } else if (value.type() == typeid(std::shared_ptr<Pointer>)) {
            auto p = std::any_cast<std::shared_ptr<Pointer>>(value);
            std::cout << "<ptr#" << p->blockIndex << "+" << p->offset << ">";
        } else if (value.type() == typeid(std::shared_ptr<Callable>)) {
            std::cout << "<fn>";
        } else {
            std::cout << "<value>";
        }
    }

private:
    struct MemoryBlock {
        std::vector<uint8_t> data;
        bool freed = false;
    };
    struct Pointer {
        int blockIndex = -1;
        size_t offset = 0;
    };
    std::vector<std::shared_ptr<MemoryBlock>> heap;

    static int64_t asIndex(const std::any& a, const char* what) {
        if (a.type() != typeid(double)) throw std::runtime_error(std::string("Expected number for ") + what + ".");
        double d = std::any_cast<double>(a);
        if (!(d == d)) throw std::runtime_error("NaN not allowed.");
        return static_cast<int64_t>(d);
    }
    static int clampByte(int64_t v) {
        if (v < 0) return 0; if (v > 255) return 255; return static_cast<int>(v);
    }
    std::any makePointer(size_t size) {
        auto block = std::make_shared<MemoryBlock>();
        block->data.assign(size, 0);
        int idx = static_cast<int>(heap.size());
        heap.push_back(block);
        auto p = std::make_shared<Pointer>();
        p->blockIndex = idx; p->offset = 0;
        return p;
    }
    std::shared_ptr<Pointer> asPointer(const std::any& a) {
        if (a.type() != typeid(std::shared_ptr<Pointer>)) throw std::runtime_error("Expected pointer.");
        auto p = std::any_cast<std::shared_ptr<Pointer>>(a);
        if (p->blockIndex < 0 || p->blockIndex >= static_cast<int>(heap.size())) throw std::runtime_error("Dangling pointer.");
        auto blk = heap[p->blockIndex];
        if (!blk || blk->freed) throw std::runtime_error("Use-after-free.");
        return p;
    }
    void freePointer(const std::shared_ptr<Pointer>& p) {
        auto blk = heap[p->blockIndex];
        if (!blk || blk->freed) throw std::runtime_error("Double free or invalid pointer.");
        blk->freed = true;
        blk->data.clear();
        blk->data.shrink_to_fit();
    }
    std::any offsetPointer(const std::shared_ptr<Pointer>& p, int64_t delta) {
        auto np = std::make_shared<Pointer>(*p);
        if (delta < 0 && static_cast<size_t>(-delta) > np->offset) throw std::runtime_error("Pointer underflow.");
        np->offset = static_cast<size_t>(static_cast<int64_t>(np->offset) + delta);
        return np;
    }
    uint8_t load8(const std::shared_ptr<Pointer>& p, int64_t off) {
        auto blk = heap[p->blockIndex];
        size_t idx = p->offset + static_cast<size_t>(off);
        if (idx >= blk->data.size()) throw std::runtime_error("read8 out of bounds.");
        return blk->data[idx];
    }
    void store8(const std::shared_ptr<Pointer>& p, int64_t off, uint8_t v) {
        auto blk = heap[p->blockIndex];
        size_t idx = p->offset + static_cast<size_t>(off);
        if (idx >= blk->data.size()) throw std::runtime_error("write8 out of bounds.");
        blk->data[idx] = v;
    }
    uint32_t load32(const std::shared_ptr<Pointer>& p, int64_t off) {
        auto blk = heap[p->blockIndex];
        size_t idx = p->offset + static_cast<size_t>(off);
        if (idx + 4 > blk->data.size()) throw std::runtime_error("read32 out of bounds.");
        uint32_t v = 0;
        v |= static_cast<uint32_t>(blk->data[idx]);
        v |= static_cast<uint32_t>(blk->data[idx+1]) << 8;
        v |= static_cast<uint32_t>(blk->data[idx+2]) << 16;
        v |= static_cast<uint32_t>(blk->data[idx+3]) << 24;
        return v;
    }
    void store32(const std::shared_ptr<Pointer>& p, int64_t off, uint32_t v) {
        auto blk = heap[p->blockIndex];
        size_t idx = p->offset + static_cast<size_t>(off);
        if (idx + 4 > blk->data.size()) throw std::runtime_error("write32 out of bounds.");
        blk->data[idx] = static_cast<uint8_t>(v & 0xFF);
        blk->data[idx+1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        blk->data[idx+2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        blk->data[idx+3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    }
    const char* stmtTypeName(const Stmt* s) {
        if (dynamic_cast<const ExpressionStmt*>(s)) return "ExpressionStmt";
        if (dynamic_cast<const LetStmt*>(s)) return "LetStmt";
        if (dynamic_cast<const FunctionStmt*>(s)) return "FunctionStmt";
        if (dynamic_cast<const ReturnStmt*>(s)) return "ReturnStmt";
        if (dynamic_cast<const BlockStmt*>(s)) return "BlockStmt";
        if (dynamic_cast<const SetStmt*>(s)) return "SetStmt";
        if (dynamic_cast<const IfStmt*>(s)) return "IfStmt";
        if (dynamic_cast<const SetIndexStmt*>(s)) return "SetIndexStmt";
        if (dynamic_cast<const ForEachStmt*>(s)) return "ForEachStmt";
        if (dynamic_cast<const WhileStmt*>(s)) return "WhileStmt";
        return "Stmt";
    }

    std::string anyToDebugString(const std::any& value) {
        std::ostringstream oss;
        if (!value.has_value() || value.type() == typeid(std::nullptr_t)) {
            oss << "nil";
        } else if (value.type() == typeid(bool)) {
            oss << (std::any_cast<bool>(value) ? "true" : "false");
        } else if (value.type() == typeid(double)) {
            oss << std::any_cast<double>(value);
        } else if (value.type() == typeid(std::string)) {
            oss << '"' << std::any_cast<std::string>(value) << '"';
        } else if (value.type() == typeid(std::shared_ptr<std::vector<std::any>>)) {
            auto vec = std::any_cast<std::shared_ptr<std::vector<std::any>>>(value);
            oss << "[";
            for (size_t i = 0; i < vec->size(); ++i) {
                oss << anyToDebugString((*vec)[i]);
                if (i + 1 < vec->size()) oss << ", ";
            }
            oss << "]";
        } else if (value.type() == typeid(std::shared_ptr<Callable>)) {
            oss << "<fn>";
        } else {
            oss << "<value>";
        }
        return oss.str();
    }
};

std::any Function::call(Interpreter& interpreter, const std::vector<std::any>& arguments) {
    auto env = std::make_shared<Environment>(getClosure());
    auto decl = getDeclaration();

    for (int i = 0; i < decl->params.size(); ++i) {
        env->define(decl->params[i].name.lexeme, arguments[i]);
    }

    auto prev = interpreter.environment;
    interpreter.environment = env;
    try {
        // Execute all but the last statement
        if (!decl->body.empty()) {
            for (size_t i = 0; i + 1 < decl->body.size(); ++i) {
                interpreter.execute(decl->body[i].get());
            }
            // Handle last statement: if it's an expression, return its value implicitly
            const Stmt* last = decl->body.back().get();
            if (const auto* exprStmt = dynamic_cast<const ExpressionStmt*>(last)) {
                std::any v = interpreter.evaluate(exprStmt->expression.get());
                interpreter.environment = prev;
                return v;
            }
            // Otherwise execute it and fall through to nil
            interpreter.execute(const_cast<Stmt*>(last));
        }
    } catch (Return& ret) {
        interpreter.environment = prev;
        return ret.value;
    }
    interpreter.environment = prev;
    return {};
}

