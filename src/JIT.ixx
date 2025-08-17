export module JIT;

import Token;
import Expr;
import Stmt;
import <vector>;
import <cstdint>;
import <cstring>;
import <memory>;
import <stdexcept>;

// Windows kernel32 prototypes at global scope
extern "C" __declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, unsigned long, unsigned long);
extern "C" __declspec(dllimport) int __stdcall VirtualFree(void*, size_t, unsigned long);

// Minimal x86-64 JIT for numeric expressions (double precision)
// Windows x64 ABI: return double in XMM0. No external calls; uses stack for temporary save.

export class X86_64JIT {
public:
    X86_64JIT() = default;

    // Compile an expression to machine code and execute it, returning the double result.
    double executeExpression(const Expr* expr) {
        reset();
        emitExpr(expr);
        emitRet();
        void* fn = finalize();
        using Fn = double(*)();
        Fn f = reinterpret_cast<Fn>(fn);
        double out = f();
        release();
        return out;
    }

    // Returns whether the expr tree is supported by the JIT (literals, grouping, binary + - * / only)
    bool canCompile(const Expr* expr) {
        if (expr == nullptr) return false;
        if (auto lit = dynamic_cast<const Literal*>(expr)) {
            return std::holds_alternative<double>(lit->value);
        }
        if (auto grp = dynamic_cast<const Grouping*>(expr)) {
            return canCompile(grp->expression.get());
        }
        if (auto bin = dynamic_cast<const Binary*>(expr)) {
            switch (bin->op.type) {
            case TokenType::PLUS:
            case TokenType::MINUS:
            case TokenType::ASTERISK:
            case TokenType::SLASH:
                return canCompile(bin->left.get()) && canCompile(bin->right.get());
            default:
                return false;
            }
        }
        return false;
    }

private:
    std::vector<std::uint8_t> code;
    std::vector<std::uint64_t> literals; // raw 64-bit doubles
    struct Fixup { size_t codeOffset; size_t literalIndex; };
    std::vector<Fixup> fixups;
    void* allocated = nullptr;
    size_t allocatedSize = 0;

    void reset() {
        code.clear(); literals.clear(); fixups.clear();
        if (allocated) { release(); }
    }

    void emitRet() { code.push_back(0xC3); }

    void emit(const std::uint8_t* bytes, size_t n) { code.insert(code.end(), bytes, bytes + n); }
    void emit1(std::uint8_t b) { code.push_back(b); }

    size_t addLiteralDouble(double v) {
        std::uint64_t raw;
        static_assert(sizeof(double) == sizeof(std::uint64_t));
        std::memcpy(&raw, &v, sizeof(double));
        literals.push_back(raw);
        return literals.size() - 1;
    }

    void emitMovRaxImm64Placeholder(size_t literalIndex) {
        // 48 B8 imm64
        emit1(0x48); emit1(0xB8);
        size_t off = code.size();
        std::uint64_t zero = 0;
        emit(reinterpret_cast<std::uint8_t*>(&zero), 8);
        fixups.push_back(Fixup{ off, literalIndex });
    }

    void emitLoadXmm0FromRax() {
        // MOVSD XMM0, qword ptr [RAX]  => F2 0F 10 00
        const std::uint8_t insn[] = { 0xF2, 0x0F, 0x10, 0x00 };
        emit(insn, sizeof(insn));
    }

    void emitMovsdStoreXmm0ToRsp() {
        // SUB RSP, 8   => 48 83 EC 08
        const std::uint8_t subRsp[] = { 0x48, 0x83, 0xEC, 0x08 };
        emit(subRsp, sizeof(subRsp));
        // MOVSD qword ptr [RSP], XMM0  => F2 0F 11 04 24
        const std::uint8_t store[] = { 0xF2, 0x0F, 0x11, 0x04, 0x24 };
        emit(store, sizeof(store));
    }

    void emitMovsdLoadXmm1FromRsp() {
        // MOVSD XMM1, qword ptr [RSP]  => F2 0F 10 0C 24
        const std::uint8_t load[] = { 0xF2, 0x0F, 0x10, 0x0C, 0x24 };
        emit(load, sizeof(load));
        // ADD RSP, 8   => 48 83 C4 08
        const std::uint8_t addRsp[] = { 0x48, 0x83, 0xC4, 0x08 };
        emit(addRsp, sizeof(addRsp));
    }

    void emitBinaryOp(TokenType op) {
        switch (op) {
        case TokenType::PLUS: {
            // ADDSD XMM0, XMM1 => F2 0F 58 C1
            const std::uint8_t b[] = { 0xF2, 0x0F, 0x58, 0xC1 };
            emit(b, sizeof(b));
            break;
        }
        case TokenType::MINUS: {
            // SUBSD XMM0, XMM1 => F2 0F 5C C1
            const std::uint8_t b[] = { 0xF2, 0x0F, 0x5C, 0xC1 };
            emit(b, sizeof(b));
            break;
        }
        case TokenType::ASTERISK: {
            // MULSD XMM0, XMM1 => F2 0F 59 C1
            const std::uint8_t b[] = { 0xF2, 0x0F, 0x59, 0xC1 };
            emit(b, sizeof(b));
            break;
        }
        case TokenType::SLASH: {
            // DIVSD XMM0, XMM1 => F2 0F 5E C1
            const std::uint8_t b[] = { 0xF2, 0x0F, 0x5E, 0xC1 };
            emit(b, sizeof(b));
            break;
        }
        default:
            throw std::runtime_error("unsupported binary op");
        }
    }

    void emitExpr(const Expr* e) {
        if (auto lit = dynamic_cast<const Literal*>(e)) {
            if (!std::holds_alternative<double>(lit->value)) throw std::runtime_error("literal type not supported by JIT");
            size_t li = addLiteralDouble(std::get<double>(lit->value));
            emitMovRaxImm64Placeholder(li);
            emitLoadXmm0FromRax();
            return;
        }
        if (auto grp = dynamic_cast<const Grouping*>(e)) {
            emitExpr(grp->expression.get());
            return;
        }
        if (auto bin = dynamic_cast<const Binary*>(e)) {
            // left -> XMM0, spill; right -> XMM0; load spill to XMM1; op XMM0, XMM1
            emitExpr(bin->left.get());
            emitMovsdStoreXmm0ToRsp();
            emitExpr(bin->right.get());
            emitMovsdLoadXmm1FromRsp();
            emitBinaryOp(bin->op.type);
            return;
        }
        throw std::runtime_error("expression form not supported by native JIT yet");
    }

    void* finalize() {
        // allocate RWX and lay out: [code][literals]
        size_t literalBytes = literals.size() * 8;
        allocatedSize = code.size() + literalBytes;
        void* mem = virtualAllocRWX(allocatedSize);
        if (!mem) throw std::runtime_error("VirtualAlloc failed");
        allocated = mem;
        std::uint8_t* base = reinterpret_cast<std::uint8_t*>(mem);
        // copy code
        if (!code.empty()) std::memcpy(base, code.data(), code.size());
        // copy literals
        std::uint8_t* litBase = base + code.size();
        for (size_t i = 0; i < literals.size(); ++i) {
            std::uint64_t raw = literals[i];
            std::memcpy(litBase + i * 8, &raw, 8);
        }
        // apply fixups
        for (const auto& fx : fixups) {
            std::uint64_t ptr = reinterpret_cast<std::uint64_t>(litBase + fx.literalIndex * 8);
            std::memcpy(base + fx.codeOffset, &ptr, 8);
        }
        return base;
    }

    void release() {
        if (allocated) {
            virtualFree(allocated, allocatedSize);
            allocated = nullptr; allocatedSize = 0;
        }
    }

    static void* virtualAllocRWX(size_t size) {
        constexpr unsigned long MEM_COMMIT = 0x1000;
        constexpr unsigned long MEM_RESERVE = 0x2000;
        constexpr unsigned long PAGE_EXECUTE_READWRITE = 0x40;
        return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }

    static void virtualFree(void* p, size_t /*size*/) {
        constexpr unsigned long MEM_RELEASE = 0x8000;
        if (p) { (void)VirtualFree(p, 0, MEM_RELEASE); }
    }
};


