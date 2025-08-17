export module Function;

import Callable;
import Environment;
import Stmt;
import <memory>;
import <any>;
import <string>;

export class Function : public Callable {
private:
    const FunctionStmt* declaration;
    std::shared_ptr<Environment> closure;

public:
    Function(const FunctionStmt* declaration, std::shared_ptr<Environment> closure)
        : declaration(declaration), closure(closure) {}

    int arity() override {
        return (int)declaration->params.size();
    }

    const FunctionStmt* getDeclaration() const { return declaration; }
    std::shared_ptr<Environment> getClosure() const { return closure; }

    std::any call(Interpreter& interpreter, const std::vector<std::any>& arguments) override;

    std::string toString() override {
        return "<fn " + declaration->name.lexeme + ">";
    }
};

