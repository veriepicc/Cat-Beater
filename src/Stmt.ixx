export module Stmt;

import Expr;
import Token;
import Type;
import <memory>;
import <vector>;
import <any>;

// Forward declarations
struct ExpressionStmt;
struct FunctionStmt;
struct LetStmt;
struct ReturnStmt;
struct BlockStmt;
struct SetStmt;
struct IfStmt;
struct SetIndexStmt;
struct ForEachStmt;
struct WhileStmt;

export struct StmtVisitor {
    virtual std::any visit(const ExpressionStmt& stmt) = 0;
    virtual std::any visit(const FunctionStmt& stmt) = 0;
    virtual std::any visit(const LetStmt& stmt) = 0;
    virtual std::any visit(const ReturnStmt& stmt) = 0;
    virtual std::any visit(const BlockStmt& stmt) = 0;
    virtual std::any visit(const SetStmt& stmt) = 0;
    virtual std::any visit(const IfStmt& stmt) = 0;
    virtual std::any visit(const SetIndexStmt& stmt) = 0;
    virtual std::any visit(const ForEachStmt& stmt) = 0;
    virtual std::any visit(const WhileStmt& stmt) = 0;
};

export struct Stmt {
    virtual ~Stmt() = default;
    virtual std::any accept(StmtVisitor& visitor) const = 0;
};

export struct ExpressionStmt : Stmt {
    ExpressionStmt(std::unique_ptr<Expr> expression)
        : expression(std::move(expression)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    std::unique_ptr<Expr> expression;
};

export struct LetStmt : Stmt {
    LetStmt(Token name, std::unique_ptr<Type> type, std::unique_ptr<Expr> initializer)
        : name(name), type(std::move(type)), initializer(std::move(initializer)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    Token name;
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> initializer;
};

export struct Parameter {
    Token name;
    std::unique_ptr<Type> type;
};

export struct FunctionStmt : Stmt {
    FunctionStmt(Token name, std::vector<Parameter> params, std::unique_ptr<Type> returnType, std::vector<std::unique_ptr<Stmt>> body)
        : name(name), params(std::move(params)), returnType(std::move(returnType)), body(std::move(body)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    Token name;
    std::vector<Parameter> params;
    std::unique_ptr<Type> returnType;
    std::vector<std::unique_ptr<Stmt>> body;
};

export struct ReturnStmt : Stmt {
    ReturnStmt(Token keyword, std::unique_ptr<Expr> value)
        : keyword(keyword), value(std::move(value)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    Token keyword;
    std::unique_ptr<Expr> value;
};

export struct BlockStmt : Stmt {
    BlockStmt(std::vector<std::unique_ptr<Stmt>> statements)
        : statements(std::move(statements)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    std::vector<std::unique_ptr<Stmt>> statements;
};

export struct SetStmt : Stmt {
    SetStmt(Token name, std::unique_ptr<Expr> value)
        : name(name), value(std::move(value)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    Token name;
    std::unique_ptr<Expr> value;
};

export struct IfStmt : Stmt {
    IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> thenBranch, std::unique_ptr<Stmt> elseBranch)
        : condition(std::move(condition)), thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

export struct SetIndexStmt : Stmt {
    SetIndexStmt(std::unique_ptr<Expr> arrayExpr, std::unique_ptr<Expr> indexExpr, std::unique_ptr<Expr> value)
        : arrayExpr(std::move(arrayExpr)), indexExpr(std::move(indexExpr)), value(std::move(value)) {}

    std::any accept(StmtVisitor& visitor) const override { return visitor.visit(*this); }

    std::unique_ptr<Expr> arrayExpr;
    std::unique_ptr<Expr> indexExpr;
    std::unique_ptr<Expr> value;
};

export struct ForEachStmt : Stmt {
    ForEachStmt(Token varName, std::unique_ptr<Expr> iterable, std::unique_ptr<Stmt> body)
        : varName(varName), iterable(std::move(iterable)), body(std::move(body)) {}

    std::any accept(StmtVisitor& visitor) const override { return visitor.visit(*this); }

    Token varName;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<Stmt> body;
};

export struct WhileStmt : Stmt {
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body)
        : condition(std::move(condition)), body(std::move(body)) {}

    std::any accept(StmtVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};
