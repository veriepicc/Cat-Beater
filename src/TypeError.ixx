export module TypeError;

import <stdexcept>;
import <string>;

export class TypeError : public std::runtime_error {
public:
    TypeError(const std::string& message) : std::runtime_error(message) {}
};

