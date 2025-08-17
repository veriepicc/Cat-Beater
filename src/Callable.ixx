export module Callable;

import <vector>;
import <any>;
import <string>;

export class Interpreter;

export class Callable {
public:
    virtual ~Callable() = default;
    virtual int arity() = 0;
    virtual std::any call(Interpreter& interpreter, const std::vector<std::any>& arguments) = 0;
    virtual std::string toString() = 0;
    virtual bool variadic() { return false; }
};

