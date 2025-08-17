export module Expr;

import Token;
import <memory>;
import <vector>;
import <any>;
import <string>;
import <variant>;

// Forward declarations
struct Binary;
struct Grouping;
struct Literal;
struct Unary;
struct Variable;
struct Assign;
struct Call;
struct ArrayLiteral;
struct Index;

export struct ExprVisitor {
    virtual std::any visit(const Binary& expr) = 0;
    virtual std::any visit(const Grouping& expr) = 0;
    virtual std::any visit(const Literal& expr) = 0;
    virtual std::any visit(const Unary& expr) = 0;
    virtual std::any visit(const Variable& expr) = 0;
    virtual std::any visit(const Assign& expr) = 0;
    virtual std::any visit(const Call& expr) = 0;
    virtual std::any visit(const ArrayLiteral& expr) = 0;
    virtual std::any visit(const Index& expr) = 0;
};

export struct Expr {
    virtual ~Expr() = default;
    virtual std::any accept(ExprVisitor& visitor) const = 0;
};

export struct Binary : Expr {
    Binary(std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right)
        : left(std::move(left)), op(op), right(std::move(right)) {}

    std::any accept(ExprVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const std::unique_ptr<Expr> left;
    const Token op;
    const std::unique_ptr<Expr> right;
};

export struct Unary : Expr {
    Unary(Token op, std::unique_ptr<Expr> right)
        : op(op), right(std::move(right)) {}

    std::any accept(ExprVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const Token op;
    const std::unique_ptr<Expr> right;
};

export struct Literal : Expr {
    Literal(std::variant<std::monostate, double, std::string, bool> value)
        : value(value) {}

    // Allow constructing from the token literal variant (monostate | double | string)
    Literal(const std::variant<std::monostate, double, std::string>& tokenLiteral)
        : value(convertFromTokenLiteral(tokenLiteral)) {}

    // Convenience overloads to avoid ambiguous construction
    explicit Literal(double d) : value(d) {}
    explicit Literal(bool b) : value(b) {}
    explicit Literal(const std::string& s) : value(s) {}

    std::any accept(ExprVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const std::variant<std::monostate, double, std::string, bool> value;

private:
    static std::variant<std::monostate, double, std::string, bool>
    convertFromTokenLiteral(const std::variant<std::monostate, double, std::string>& t) {
        if (std::holds_alternative<double>(t)) return std::get<double>(t);
        if (std::holds_alternative<std::string>(t)) return std::get<std::string>(t);
        return std::monostate{};
    }
};

export struct Grouping : Expr {
    Grouping(std::unique_ptr<Expr> expression)
        : expression(std::move(expression)) {}

    std::any accept(ExprVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const std::unique_ptr<Expr> expression;
};

export struct Variable : Expr {
    Variable(Token name) : name(name) {}

    std::any accept(ExprVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const Token name;
};

export struct Assign : Expr {
    Assign(Token name, std::unique_ptr<Expr> value)
        : name(name), value(std::move(value)) {}
    
    std::any accept(ExprVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const Token name;
    const std::unique_ptr<Expr> value;
};

export struct Call : Expr {
    Call(std::unique_ptr<Expr> callee, Token paren, std::vector<std::unique_ptr<Expr>> arguments)
        : callee(std::move(callee)), paren(paren), arguments(std::move(arguments)) {}

    std::any accept(ExprVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const std::unique_ptr<Expr> callee;
    const Token paren; // for error reporting
    const std::vector<std::unique_ptr<Expr>> arguments;
};

// Array literal: [ expr, expr, ... ]
export struct ArrayLiteral : Expr {
    ArrayLiteral(std::vector<std::unique_ptr<Expr>> elements)
        : elements(std::move(elements)) {}

    std::any accept(ExprVisitor& visitor) const override { return visitor.visit(*this); }

    const std::vector<std::unique_ptr<Expr>> elements;
};

// Indexing: array[index]
export struct Index : Expr {
    Index(std::unique_ptr<Expr> array, Token bracket, std::unique_ptr<Expr> index)
        : array(std::move(array)), bracket(bracket), index(std::move(index)) {}

    std::any accept(ExprVisitor& visitor) const override { return visitor.visit(*this); }

    const std::unique_ptr<Expr> array;
    const Token bracket;
    const std::unique_ptr<Expr> index;
};
