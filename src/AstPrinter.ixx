export module AstPrinter;

import Expr;
import <string>;
import <vector>;
import <memory>;
import Stmt;
import Type;
import <any>;

export class AstPrinter : public ExprVisitor, public StmtVisitor, public TypeVisitor {
public:
    std::string print(const std::vector<std::unique_ptr<Stmt>>& statements) {
        std::string result;
        for (const auto& stmt : statements) {
            if (stmt) {
                result += std::any_cast<std::string>(stmt->accept(*this));
                result += "\n";
            }
        }
        return result;
    }

    std::any visit(const ExpressionStmt& stmt) override {
        return parenthesize<Expr>(";", {stmt.expression.get()});
    }

    std::any visit(const LetStmt& stmt) override {
        std::string name = "let " + stmt.name.lexeme + ": " + std::any_cast<std::string>(stmt.type->accept(*this));
        if (stmt.initializer) {
            return parenthesize<Expr>(name, {stmt.initializer.get()});
        }
        return parenthesize<Expr>(name, {});
    }

    std::any visit(const FunctionStmt& stmt) override {
        std::string name = "fn " + stmt.name.lexeme + "(";
        for(size_t i = 0; i < stmt.params.size(); ++i) {
            name += stmt.params[i].name.lexeme + ": " + std::any_cast<std::string>(stmt.params[i].type->accept(*this));
            if (i < stmt.params.size() - 1) name += " ";
        }
        name += ") -> " + std::any_cast<std::string>(stmt.returnType->accept(*this));
        
        std::vector<const Stmt*> stmts;
        for (const auto& s : stmt.body) {
            stmts.push_back(s.get());
        }

        return parenthesize<Stmt>(name, stmts);
    }
    
    std::any visit(const BlockStmt& stmt) override {
        std::vector<const Stmt*> stmts;
        for (const auto& s : stmt.statements) {
            stmts.push_back(s.get());
        }
        return parenthesize<Stmt>("block", stmts);
    }
    
    std::any visit(const ReturnStmt& stmt) override {
        if (stmt.value) {
            return parenthesize<Expr>("return", {stmt.value.get()});
        }
        return parenthesize<Expr>("return", {});
    }

    std::any visit(const SetStmt& stmt) override {
        return parenthesize<Expr>("set! " + stmt.name.lexeme, {stmt.value.get()});
    }

    std::any visit(const Binary& expr) override {
        return parenthesize<Expr>(expr.op.lexeme, {expr.left.get(), expr.right.get()});
    }

    std::any visit(const Grouping& expr) override {
        return parenthesize<Expr>("group", {expr.expression.get()});
    }

    std::any visit(const Literal& expr) override {
        if (std::holds_alternative<std::string>(expr.value)) {
            return std::get<std::string>(expr.value);
        } else if (std::holds_alternative<double>(expr.value)) {
            return std::to_string(std::get<double>(expr.value));
        }
        return std::string("nil");
    }

    std::any visit(const Unary& expr) override {
        return parenthesize<Expr>(expr.op.lexeme, {expr.right.get()});
    }

    std::any visit(const Variable& expr) override {
        return expr.name.lexeme;
    }

    std::any visit(const Assign& expr) override {
        return parenthesize<Expr>("= " + expr.name.lexeme, {expr.value.get()});
    }

    std::any visit(const Call& expr) override {
        std::vector<const Expr*> exprs;
        exprs.push_back(expr.callee.get());
        for (const auto& arg : expr.arguments) {
            exprs.push_back(arg.get());
        }
        return parenthesize("call", exprs);
    }

    std::any visit(const PrimitiveType& type) override {
        return type.keyword.lexeme;
    }

    std::any visit(const PointerType& type) override {
        return parenthesize<Type>("ptr", {type.pointee.get()});
    }


private:
    template<typename T>
    std::string parenthesize(const std::string& name, const std::vector<const T*>& nodes) {
        std::string builder = "(";
        builder += name;
        for (const T* node : nodes) {
            builder += " ";
            if (node)
                builder += std::any_cast<std::string>(node->accept(*this));
            else
                builder += "nil";

        }
        builder += ")";
        return builder;
    }
};
