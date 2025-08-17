export module TypeChecker;

import Expr;
import Stmt;
import Type;
import Token;
import TypeError;
import <memory>;
import <vector>;
import <string>;
import <unordered_map>;
import <any>;
import <iostream>;

export class TypeChecker : public ExprVisitor, public StmtVisitor {
private:
    int indent = 0;
    void log(const std::string& message) {
        for (int i = 0; i < indent; ++i) std::cout << "  ";
        std::cout << message << std::endl;
    }

public:
    TypeChecker() {
        // Seed builtin: print : (string) -> nil
        std::vector<std::unique_ptr<Type>> paramTypes;
        paramTypes.push_back(std::make_unique<PrimitiveType>(Token{ TokenType::STRING_LITERAL, "string", {}, -1 }));
        auto retType = std::make_unique<PrimitiveType>(Token{ TokenType::NIL, "nil", {}, -1 });
        auto printFn = std::make_shared<FunctionType>(std::move(paramTypes), std::move(retType));
        environment["print"] = std::static_pointer_cast<Type>(printFn);
    }

    void check(const std::vector<std::unique_ptr<Stmt>>& statements) {
        // log("--- Starting Type Check ---");
        for (const auto& statement : statements) {
            check(statement.get());
        }
        // log("--- Finished Type Check ---");
    }

private:
    void check(const std::vector<std::unique_ptr<Stmt>>& statements, std::unordered_map<std::string, std::shared_ptr<Type>> newEnvironment) {
        std::unordered_map<std::string, std::shared_ptr<Type>> oldEnvironment = std::move(this->environment);
        this->environment = std::move(newEnvironment);
        for (const auto& statement : statements) {
            check(statement.get());
        }
        this->environment = std::move(oldEnvironment);
    }

    void check(Stmt* stmt) {
        // indent++; // keep for potential future debug
        stmt->accept(*this);
        // indent--;
    }

    std::shared_ptr<Type> check(Expr* expr) {
        // indent++;
        auto result = std::any_cast<std::shared_ptr<Type>>(expr->accept(*this));
        // indent--;
        return result;
    }

    // StmtVisitor methods
    std::any visit(const ExpressionStmt& stmt) override {
        // log("Visit ExpressionStmt");
        check(stmt.expression.get());
        return {};
    }

    std::any visit(const LetStmt& stmt) override {
        // log("Visit LetStmt");
        std::shared_ptr<Type> valueType = check(stmt.initializer.get());
        if (*valueType != *stmt.type) {
            throw TypeError("Initializer type does not match variable type.");
        }
        environment[stmt.name.lexeme] = std::shared_ptr<Type>(stmt.type->clone());
        return {};
    }

    std::any visit(const FunctionStmt& stmt) override {
        // log("Visit FunctionStmt");
        std::vector<std::unique_ptr<Type>> paramTypes;
        for (const auto& param : stmt.params) {
            paramTypes.push_back(param.type->clone());
        }
        auto functionType = std::make_shared<FunctionType>(std::move(paramTypes), stmt.returnType->clone());
        environment[stmt.name.lexeme] = functionType;

        std::shared_ptr<Type> previousFunction = std::move(currentFunction);
        currentFunction = std::shared_ptr<Type>(stmt.returnType->clone());

        std::unordered_map<std::string, std::shared_ptr<Type>> newEnv = environment;
        for (const auto& param : stmt.params) {
            newEnv[param.name.lexeme] = std::shared_ptr<Type>(param.type->clone());
        }
        check(stmt.body, newEnv);
        currentFunction = std::move(previousFunction);
        return {};
    }

    std::any visit(const ReturnStmt& stmt) override {
        // log("Visit ReturnStmt");
        if (currentFunction == nullptr) throw TypeError("Cannot return from top-level code.");
        if (stmt.value == nullptr) {
            auto prim = std::dynamic_pointer_cast<PrimitiveType>(currentFunction);
            if (!prim || prim->keyword.type != TokenType::NIL) {
               throw TypeError("Cannot return nothing from a function with a non-nil return type.");
            }
        } else {
            std::shared_ptr<Type> valueType = check(stmt.value.get());
            if (*valueType != *currentFunction) {
                throw TypeError("Return value type does not match function return type.");
            }
        }
        return {};
    }

    std::any visit(const BlockStmt& stmt) override {
        // log("Visit BlockStmt");
        check(stmt.statements, environment);
        return {};
    }

    std::any visit(const SetStmt& stmt) override {
        // log("Visit SetStmt");
        return {};
    }

    std::any visit(const IfStmt& stmt) override {
        // log("Visit IfStmt");
        std::shared_ptr<Type> conditionType = check(stmt.condition.get());
        auto primitiveCondition = std::dynamic_pointer_cast<PrimitiveType>(conditionType);
        if (!primitiveCondition || primitiveCondition->keyword.type != TokenType::BOOL) {
            throw TypeError("If condition must be a boolean.");
        }
        check(stmt.thenBranch.get());
        if (stmt.elseBranch != nullptr) {
            check(stmt.elseBranch.get());
        }
        return {};
    }

    // ExprVisitor methods
    std::any visit(const Binary& expr) override {
        // log("Visit Binary");
        std::shared_ptr<Type> left = check(expr.left.get());
        std::shared_ptr<Type> right = check(expr.right.get());
        if (*left != *right) throw TypeError("Binary operator applied to different types.");
        switch (expr.op.type) {
        case TokenType::PLUS: case TokenType::MINUS: case TokenType::SLASH: case TokenType::ASTERISK:
            return std::any(left);
        case TokenType::GREATER: case TokenType::GREATER_EQUAL: case TokenType::LESS: case TokenType::LESS_EQUAL: case TokenType::EQUAL_EQUAL: case TokenType::BANG_EQUAL:
            return std::any(std::static_pointer_cast<Type>(
                std::make_shared<PrimitiveType>(Token{ TokenType::BOOL, "bool", {}, -1 })
            ));
        }
        return {};
    }

    std::any visit(const Grouping& expr) override {
        // log("Visit Grouping");
        return std::any(check(expr.expression.get()));
    }

    std::any visit(const Literal& expr) override {
        // log("Visit Literal");
        if (std::holds_alternative<double>(expr.value))
            return std::any(std::static_pointer_cast<Type>(
                std::make_shared<PrimitiveType>(Token{ TokenType::F64, "f64", {}, -1 })
            ));
        if (std::holds_alternative<std::string>(expr.value))
            return std::any(std::static_pointer_cast<Type>(
                std::make_shared<PrimitiveType>(Token{ TokenType::STRING_LITERAL, "string", {}, -1 })
            ));
        return std::any(std::static_pointer_cast<Type>(
            std::make_shared<PrimitiveType>(Token{ TokenType::NIL, "nil", {}, -1 })
        ));
    }

    std::any visit(const Unary& expr) override {
        // log("Visit Unary");
        return std::any(check(expr.right.get()));
    }

    std::any visit(const Variable& expr) override {
        // log("Visit Variable");
        if (environment.find(expr.name.lexeme) == environment.end()) {
            throw TypeError("Undefined variable '" + expr.name.lexeme + "'.");
        }
        return std::any(environment.at(expr.name.lexeme));
    }

    std::any visit(const Assign& expr) override {
        // log("Visit Assign");
        return {};
    }

    std::any visit(const Call& expr) override {
        // log("Visit Call");
        // Special-case builtin print: allow any number of arguments, return nil
        if (const Variable* var = dynamic_cast<const Variable*>(expr.callee.get())) {
            if (var->name.lexeme == "print") {
                for (const auto& arg : expr.arguments) { check(arg.get()); }
                return std::any(std::static_pointer_cast<Type>(
                    std::make_shared<PrimitiveType>(Token{ TokenType::NIL, "nil", {}, -1 })
                ));
            }
        }

        std::shared_ptr<Type> calleeType = check(expr.callee.get());
        auto functionType = std::dynamic_pointer_cast<FunctionType>(calleeType);
        if (!functionType) throw TypeError("Can only call functions.");
        if (expr.arguments.size() != functionType->params.size()) throw TypeError("Incorrect number of arguments.");
        for (size_t i = 0; i < expr.arguments.size(); ++i) {
            std::shared_ptr<Type> argType = check(expr.arguments[i].get());
            if (*argType != *functionType->params[i]) {
                throw TypeError("Incorrect argument type.");
            }
        }
        return std::any(std::shared_ptr<Type>(functionType->ret->clone()));
    }

    std::unordered_map<std::string, std::shared_ptr<Type>> environment;
    std::shared_ptr<Type> currentFunction = nullptr;
};
