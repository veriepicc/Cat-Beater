export module Type;

import Token;
import <memory>;
import <any>;
import <vector>;

// Forward declarations
struct PrimitiveType;
struct PointerType;
struct FunctionType;

export struct TypeVisitor {
    virtual std::any visit(const PrimitiveType& type) = 0;
    virtual std::any visit(const PointerType& type) = 0;
    virtual std::any visit(const FunctionType& type) = 0;
};

export struct Type {
    virtual ~Type() = default;
    virtual std::any accept(TypeVisitor& visitor) const = 0;
    virtual std::unique_ptr<Type> clone() const = 0;
    virtual bool operator==(const Type& other) const = 0;

    template<typename T>
    bool is() const {
        return dynamic_cast<const T*>(this) != nullptr;
    }

    template<typename T>
    const T* as() const {
        return dynamic_cast<const T*>(this);
    }
};

export struct PrimitiveType : Type {
    PrimitiveType(Token keyword) : keyword(keyword) {}

    std::any accept(TypeVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const Token keyword;

    std::unique_ptr<Type> clone() const override {
        return std::make_unique<PrimitiveType>(keyword);
    }

    bool operator==(const Type& other) const override {
        const auto* p = dynamic_cast<const PrimitiveType*>(&other);
        if (!p) return false;
        return keyword.type == p->keyword.type;
    }
};

export struct PointerType : Type {
    PointerType(std::unique_ptr<Type> pointee) : pointee(std::move(pointee)) {}

    std::any accept(TypeVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    const std::unique_ptr<Type> pointee;

    std::unique_ptr<Type> clone() const override {
        return std::make_unique<PointerType>(pointee->clone());
    }

    bool operator==(const Type& other) const override {
        const auto* p = dynamic_cast<const PointerType*>(&other);
        if (!p) return false;
        return *pointee == *p->pointee;
    }
};

export struct FunctionType : public Type {
public:
    std::vector<std::unique_ptr<Type>> params;
    std::unique_ptr<Type> ret;

    FunctionType(std::vector<std::unique_ptr<Type>> params, std::unique_ptr<Type> ret)
        : params(std::move(params)), ret(std::move(ret)) {}

    std::any accept(TypeVisitor& visitor) const override {
        return visitor.visit(*this);
    }

    std::unique_ptr<Type> clone() const override {
        std::vector<std::unique_ptr<Type>> newParams;
        for (const auto& param : this->params) {
            newParams.push_back(param->clone());
        }
        return std::make_unique<FunctionType>(std::move(newParams), this->ret->clone());
    }

    bool operator==(const Type& other) const override {
        const auto* p = dynamic_cast<const FunctionType*>(&other);
        if (!p) return false;
        if (this->params.size() != p->params.size()) return false;
        for (size_t i = 0; i < this->params.size(); ++i) {
            if (*this->params[i] != *p->params[i]) return false;
        }
        return *this->ret == *p->ret;
    }
};
