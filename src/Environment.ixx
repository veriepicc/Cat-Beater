export module Environment;

import <string>;
import <any>;
import <unordered_map>;
import <memory>;
import <stdexcept>;

export class Environment {
public:
    Environment() : enclosing(nullptr) {}
    Environment(std::shared_ptr<Environment> enclosing) : enclosing(enclosing) {}

    void define(const std::string& name, std::any value) {
        values[name] = value;
    }

    std::any get(const std::string& name) {
        if (values.count(name)) {
            return values.at(name);
        }

        if (enclosing != nullptr) {
            return enclosing->get(name);
        }

        throw std::runtime_error("Undefined variable '" + name + "'.");
    }

    void assign(const std::string& name, std::any value) {
        if (values.count(name)) {
            values[name] = value;
            return;
        }

        if (enclosing != nullptr) {
            enclosing->assign(name, value);
            return;
        }

        throw std::runtime_error("Undefined variable '" + name + "'.");
    }

private:
    std::unordered_map<std::string, std::any> values;
    std::shared_ptr<Environment> enclosing;
};

