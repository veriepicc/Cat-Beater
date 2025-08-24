export module VM;

import IR;
import <vector>;
import <any>;
import <string>;
import <iostream>;
import <cmath>;
import <unordered_map>;
import <fstream>;
import <string>;
using namespace std::string_literals;
import <functional>;
import <cstdlib>;
import <map>;
import <cctype>;
import Bytecode;

export class VM {
public:
    int run(const Chunk& chunk) {
        std::vector<std::any> stack;
        stack.reserve(256);
        std::unordered_map<std::string, std::any> globals;
        struct Pointer { int block = -1; size_t offset = 0; };
        struct Block { std::vector<std::uint8_t> data; bool freed=false; };
        std::vector<std::shared_ptr<Block>> heap;
        using Map = std::unordered_map<std::string, std::any>;
        struct Frame { std::uint32_t returnPC; std::vector<std::any> locals; };
        std::vector<Frame> frames;
        std::function<std::string(const std::any&)> toString;
        toString = [&](const std::any& v)->std::string{
            if (!v.has_value() || v.type()==typeid(std::nullptr_t)) return std::string("");
            if (v.type()==typeid(std::string)) return std::any_cast<std::string>(v);
            if (v.type()==typeid(double)) {
                double d = std::any_cast<double>(v);
                double tr = std::trunc(d);
                if (std::fabs(d - tr) < 1e-12) {
                    return std::to_string(static_cast<long long>(tr));
                }
                return std::to_string(d);
            }
            if (v.type()==typeid(bool)) return std::any_cast<bool>(v)?std::string("true"):std::string("false");
            if (v.type()==typeid(std::shared_ptr<std::vector<std::any>>)) {
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(v);
                std::string s("[");
                for (size_t i=0;i<arr->size();++i){ if (i) s.append(", "); s.append(toString((*arr)[i])); }
                s.push_back(']');
                return s;
            }
            return std::string("<val>");
        };
        auto readU16 = [&](size_t& pc) {
            std::uint16_t lo = chunk.code[pc++];
            std::uint16_t hi = chunk.code[pc++];
            return static_cast<std::uint16_t>(lo | (hi << 8));
        };
        size_t pc = 0;
        auto reportRuntimeError = [&](const std::string& msg){
            std::uint32_t line = 0;
            if (!chunk.debugLines.empty() && pc>0 && pc-1 < chunk.debugLines.size()) line = chunk.debugLines[pc-1];
            std::cerr << "Runtime error";
            if (!chunk.sourceName.empty()) std::cerr << " in " << chunk.sourceName;
            if (line) {
                std::cerr << ": line " << line;
                if (!chunk.debugCols.empty() && pc>0 && pc-1 < chunk.debugCols.size()) {
                    std::uint32_t col = chunk.debugCols[pc-1];
                    if (col) std::cerr << ", col " << col;
                }
            }
            std::cerr << ": " << msg << "\n";
        };
        // simple memory instrumentation for arrays/maps
        std::size_t arraysCreated = 0, arraysDestroyed = 0, mapsCreated = 0, mapsDestroyed = 0;
        auto makeArray = [&]() -> std::shared_ptr<std::vector<std::any>> {
            arraysCreated++;
            return std::shared_ptr<std::vector<std::any>>(new std::vector<std::any>(), [&](std::vector<std::any>* p){ arraysDestroyed++; delete p; });
        };
        auto makeMap = [&]() -> std::shared_ptr<Map> {
            mapsCreated++;
            return std::shared_ptr<Map>(new Map(), [&](Map* p){ mapsDestroyed++; delete p; });
        };
        bool memDbg = false;
        {
            char* envVal = nullptr; size_t envLen = 0;
#if defined(_MSC_VER)
            if (_dupenv_s(&envVal, &envLen, "CB_MEMDBG") == 0 && envVal && envLen > 0) memDbg = true;
#else
            const char* v = std::getenv("CB_MEMDBG"); if (v) memDbg = true;
#endif
            if (envVal) { free(envVal); envVal = nullptr; }
        }

        std::map<int, std::shared_ptr<std::fstream>> streams;
        int nextHandle = 3;
        while (pc < chunk.code.size()) {
            OpCode op = static_cast<OpCode>(chunk.code[pc++]);
            switch (op) {
            case OpCode::OP_JUMP: {
                std::uint16_t off = readU16(pc);
                pc += off; break; }
            case OpCode::OP_JUMP_IF_FALSE: {
                std::uint16_t off = readU16(pc);
                const std::any& v = stack.back();
                if (!isTruthy(v)) pc += off;
                break; }
            case OpCode::OP_LOOP: {
                std::uint16_t off = readU16(pc);
                pc -= off; break; }
            case OpCode::OP_CONST: {
                std::uint16_t idx = readU16(pc);
                const auto& v = chunk.constants[idx];
                if (std::holds_alternative<double>(v)) stack.push_back(std::get<double>(v));
                else if (std::holds_alternative<std::string>(v)) stack.push_back(std::get<std::string>(v));
                else if (std::holds_alternative<bool>(v)) stack.push_back(std::get<bool>(v));
                else stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                std::uint16_t ni = readU16(pc);
                const std::string& name = chunk.names[ni];
                auto it = globals.find(name);
                if (it == globals.end()) stack.push_back(nullptr);
                else stack.push_back(it->second);
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                std::uint16_t ni = readU16(pc);
                const std::string& name = chunk.names[ni];
                std::any v = stack.back(); stack.pop_back();
                globals[name] = v;
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                std::uint16_t idx = readU16(pc);
                if (frames.empty() || idx >= frames.back().locals.size()) { stack.push_back(nullptr); }
                else stack.push_back(frames.back().locals[idx]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                std::uint16_t idx = readU16(pc);
                std::any v = stack.back(); stack.pop_back();
                if (!frames.empty()) {
                    if (idx >= frames.back().locals.size()) frames.back().locals.resize(idx + 1);
                    frames.back().locals[idx] = v;
                }
                break;
            }
            case OpCode::OP_ADD: binary(stack, [](double a,double b){return a+b;}); break;
            case OpCode::OP_SUB: binary(stack, [](double a,double b){return a-b;}); break;
            case OpCode::OP_MUL: binary(stack, [](double a,double b){return a*b;}); break;
            case OpCode::OP_DIV: {
                double b = popNum(stack); double a = popNum(stack);
                if (b == 0.0) { reportRuntimeError("division by zero"); stack.push_back(0.0); }
                else { stack.push_back(a / b); }
                break;
            }
            case OpCode::OP_MOD: {
                double b = popNum(stack); double a = popNum(stack);
                if (b == 0.0) { reportRuntimeError("modulo by zero"); stack.push_back(0.0); }
                else { stack.push_back(fmod(a, b)); }
                break;
            }
            case OpCode::OP_GT: cmp(stack, [](double a,double b){return a>b;}); break;
            case OpCode::OP_GE: cmp(stack, [](double a,double b){return a>=b;}); break;
            case OpCode::OP_LT: cmp(stack, [](double a,double b){return a<b;}); break;
            case OpCode::OP_LE: cmp(stack, [](double a,double b){return a<=b;}); break;
            case OpCode::OP_EQ: beq(stack, true); break;
            case OpCode::OP_NE: beq(stack, false); break;
            case OpCode::OP_AND: {
                bool b = isTruthy(pop(stack));
                bool a = isTruthy(pop(stack));
                stack.push_back(a && b);
                break;
            }
            case OpCode::OP_OR: {
                bool b = isTruthy(pop(stack));
                bool a = isTruthy(pop(stack));
                stack.push_back(a || b);
                break;
            }
            case OpCode::OP_PRINT: {
                std::uint8_t n = chunk.code[pc++];
                std::vector<std::any> args;
                args.reserve(n);
                for (int i = 0; i < n; ++i) args.push_back(stack[stack.size() - n + i]);
                for (auto it = stack.end() - n; it != stack.end(); ++it) {}
                for (int i = 0; i < n; ++i) stack.pop_back();
                for (int i = 0; i < n; ++i) {
                    write(args[i]);
                    if (i + 1 < n) std::cout << " ";
                }
                std::cout << "\n";
                // push true to avoid propagating nil
                stack.push_back(true);
                break;
            }
            // NOTE: __join lowered to no-op const nil in codegen right now
            case OpCode::OP_NEW_ARRAY: {
                std::uint8_t n = chunk.code[pc++];
                auto arr = makeArray();
                arr->reserve(n);
                for (int i = n - 1; i >= 0; --i) { arr->insert(arr->begin(), stack.back()); stack.pop_back(); }
                stack.push_back(arr);
                break;
            }
            case OpCode::OP_INDEX_GET: {
                auto idx = popNum(stack);
                auto anyArr = pop(stack);
                if (anyArr.type() != typeid(std::shared_ptr<std::vector<std::any>>)) { stack.push_back(nullptr); break; }
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(anyArr);
                int i = static_cast<int>(idx);
                if (i < 0 || i >= static_cast<int>(arr->size())) { stack.push_back(nullptr); break; }
                stack.push_back((*arr)[i]);
                break;
            }
            case OpCode::OP_INDEX_SET: {
                auto val = pop(stack);
                auto idx = popNum(stack);
                auto anyArr = pop(stack);
                if (anyArr.type() != typeid(std::shared_ptr<std::vector<std::any>>)) { stack.push_back(nullptr); break; }
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(anyArr);
                int i = static_cast<int>(idx);
                if (i < 0 || i >= static_cast<int>(arr->size())) { stack.push_back(nullptr); break; }
                (*arr)[i] = val;
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_LEN: {
                auto anyVal = pop(stack);
                if (anyVal.type() == typeid(std::shared_ptr<std::vector<std::any>>)) {
                    auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(anyVal);
                    stack.push_back(static_cast<double>(arr->size()));
                } else if (anyVal.type() == typeid(std::string)) {
                    const std::string& s = std::any_cast<std::string>(anyVal);
                    stack.push_back(static_cast<double>(s.size()));
                } else {
                    stack.push_back(0.0);
                }
                break;
            }
            case OpCode::OP_APPEND: {
                auto val = pop(stack);
                auto anyArr = pop(stack);
                if (anyArr.type() != typeid(std::shared_ptr<std::vector<std::any>>)) { stack.push_back(nullptr); break; }
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(anyArr);
                arr->push_back(val);
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_NEW_MAP: {
                auto m = makeMap();
                stack.push_back(m);
                break;
            }
            case OpCode::OP_MAP_GET: {
                auto keyAny = pop(stack);
                auto anyMap = pop(stack);
                if (anyMap.type() != typeid(std::shared_ptr<Map>) || keyAny.type() != typeid(std::string)) { stack.push_back(nullptr); break; }
                auto m = std::any_cast<std::shared_ptr<Map>>(anyMap);
                const std::string& k = std::any_cast<std::string>(keyAny);
                auto it = m->find(k);
                stack.push_back(it==m->end()? std::any{} : it->second);
                break;
            }
            case OpCode::OP_MAP_SET: {
                auto val = pop(stack);
                auto keyAny = pop(stack);
                auto anyMap = pop(stack);
                if (anyMap.type() != typeid(std::shared_ptr<Map>) || keyAny.type() != typeid(std::string)) { stack.push_back(nullptr); break; }
                auto m = std::any_cast<std::shared_ptr<Map>>(anyMap);
                const std::string& k = std::any_cast<std::string>(keyAny);
                (*m)[k] = val;
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_MAP_HAS: {
                auto keyAny = pop(stack);
                auto anyMap = pop(stack);
                if (anyMap.type() != typeid(std::shared_ptr<Map>) || keyAny.type() != typeid(std::string)) { stack.push_back(false); break; }
                auto m = std::any_cast<std::shared_ptr<Map>>(anyMap);
                const std::string& k = std::any_cast<std::string>(keyAny);
                stack.push_back(m->find(k) != m->end());
                break;
            }
            case OpCode::OP_STR_INDEX: {
                double idx = popNum(stack);
                auto sAny = pop(stack);
                if (sAny.type() != typeid(std::string)) { stack.push_back(""s); break; }
                const std::string& s = std::any_cast<std::string>(sAny);
                int i = static_cast<int>(idx);
                if (i < 0 || i >= (int)s.size()) { stack.push_back(""s); break; }
                stack.push_back(std::string(1, s[i]));
                break;
            }
            case OpCode::OP_SUBSTR: {
                double b = popNum(stack);
                double a = popNum(stack);
                auto sAny = pop(stack);
                if (sAny.type() != typeid(std::string)) { stack.push_back(""s); break; }
                const std::string& s = std::any_cast<std::string>(sAny);
                int i = static_cast<int>(a), j = static_cast<int>(b);
                i = std::max(0, std::min(i, (int)s.size()));
                j = std::max(i, std::min(j, (int)s.size()));
                stack.push_back(s.substr(i, j - i));
                break;
            }
            case OpCode::OP_STR_FIND: {
                auto needleAny = pop(stack);
                auto hayAny = pop(stack);
                if (hayAny.type() != typeid(std::string) || needleAny.type() != typeid(std::string)) { stack.push_back(-1.0); break; }
                const std::string& h = std::any_cast<std::string>(hayAny);
                const std::string& n = std::any_cast<std::string>(needleAny);
                size_t pos = h.find(n);
                if (pos == std::string::npos) stack.push_back(-1.0); else stack.push_back(static_cast<double>(pos));
                break;
            }
            case OpCode::OP_SPLIT: {
                auto sepAny = pop(stack);
                auto strAny = pop(stack);
                if (strAny.type() != typeid(std::string) || sepAny.type() != typeid(std::string)) { stack.push_back(nullptr); break; }
                const std::string& s = std::any_cast<std::string>(strAny);
                const std::string& sep = std::any_cast<std::string>(sepAny);
                auto arr = makeArray();
                if (sep.empty()) { // split into characters
                    arr->reserve(s.size());
                    for (char c : s) arr->push_back(std::string(1, c));
                } else {
                    size_t start = 0;
                    while (true) {
                        size_t pos = s.find(sep, start);
                        if (pos == std::string::npos) {
                            arr->push_back(s.substr(start));
                            break;
                        }
                        arr->push_back(s.substr(start, pos - start));
                        start = pos + sep.size();
                    }
                }
                stack.push_back(arr);
                break;
            }
            case OpCode::OP_STR_CAT: {
                auto bAny = pop(stack);
                auto aAny = pop(stack);
                std::string as = toString(aAny);
                std::string bs = toString(bAny);
                stack.push_back(as + bs);
                break;
            }
            case OpCode::OP_STR_UPPER: {
                auto sAny = pop(stack);
                if (sAny.type()!=typeid(std::string)) { stack.push_back(""s); break; }
                std::string s = std::any_cast<std::string>(sAny);
                for (char& c : s) c = static_cast<char>(std::toupper((unsigned char)c));
                stack.push_back(s);
                break;
            }
            case OpCode::OP_STR_LOWER: {
                auto sAny = pop(stack);
                if (sAny.type()!=typeid(std::string)) { stack.push_back(""s); break; }
                std::string s = std::any_cast<std::string>(sAny);
                for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
                stack.push_back(s);
                break;
            }
            case OpCode::OP_STR_CONTAINS: {
                auto needleAny = pop(stack);
                auto hayAny = pop(stack);
                if (hayAny.type()!=typeid(std::string) || needleAny.type()!=typeid(std::string)) { stack.push_back(false); break; }
                const std::string& h = std::any_cast<std::string>(hayAny);
                const std::string& n = std::any_cast<std::string>(needleAny);
                stack.push_back(h.find(n) != std::string::npos);
                break;
            }
            case OpCode::OP_FORMAT: {
                std::uint8_t argc = chunk.code[pc++];
                std::vector<std::any> args; args.reserve(argc+1);
                for (int i=argc; i>=0; --i) { args.insert(args.begin(), pop(stack)); }
                if (args[0].type()!=typeid(std::string)) { stack.push_back(""s); break; }
                std::string fmt = std::any_cast<std::string>(args[0]);
                std::string out; out.reserve(fmt.size()+argc*4);
                size_t i=0; int ai=1; while (i<fmt.size()) {
                    if (fmt[i]=='{' && i+1<fmt.size() && fmt[i+1]=='}' && ai<(int)args.size()) { out += toString(args[ai++]); i+=2; }
                    else { out.push_back(fmt[i++]); }
                }
                stack.push_back(out);
                break;
            }
            case OpCode::OP_JOIN: {
                auto sepAny = pop(stack);
                auto arrAny = pop(stack);
                if (arrAny.type() != typeid(std::shared_ptr<std::vector<std::any>>) || sepAny.type()!=typeid(std::string)) { stack.push_back(""s); break; }
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(arrAny);
                const std::string& sep = std::any_cast<std::string>(sepAny);
                std::string s;
                for (size_t i=0;i<arr->size();++i){ if (i) s.append(sep); s.append(toString((*arr)[i])); }
                stack.push_back(s);
                break;
            }
            case OpCode::OP_TRIM: {
                auto sAny = pop(stack);
                if (sAny.type()!=typeid(std::string)) { stack.push_back(""s); break; }
                const std::string& in = std::any_cast<std::string>(sAny);
                size_t i=0,j=in.size(); while (i<j && std::isspace((unsigned char)in[i])) ++i; while (j>i && std::isspace((unsigned char)in[j-1])) --j;
                stack.push_back(in.substr(i, j-i));
                break;
            }
            case OpCode::OP_REPLACE: {
                auto replAny = pop(stack);
                auto needleAny = pop(stack);
                auto sAny = pop(stack);
                if (sAny.type()!=typeid(std::string) || needleAny.type()!=typeid(std::string) || replAny.type()!=typeid(std::string)) { stack.push_back(""s); break; }
                std::string s = std::any_cast<std::string>(sAny);
                const std::string& n = std::any_cast<std::string>(needleAny);
                const std::string& r = std::any_cast<std::string>(replAny);
                if (!n.empty()) {
                    size_t pos=0; while ((pos = s.find(n, pos)) != std::string::npos) { s.replace(pos, n.size(), r); pos += r.size(); }
                }
                stack.push_back(s);
                break;
            }
            case OpCode::OP_PACK_F64LE: {
                auto nAny = pop(stack);
                if (nAny.type() != typeid(double)) { stack.push_back(""s); break; }
                double d = std::any_cast<double>(nAny);
                std::string s; s.resize(8);
                const unsigned char* p = reinterpret_cast<const unsigned char*>(&d);
                for (int i = 0; i < 8; ++i) s[i] = static_cast<char>(p[i]);
                stack.push_back(s);
                break;
            }
            case OpCode::OP_PACK_U16LE: {
                auto nAny = pop(stack);
                if (nAny.type() != typeid(double)) { stack.push_back(""s); break; }
                std::uint16_t v = static_cast<std::uint16_t>(static_cast<unsigned long long>(std::any_cast<double>(nAny)) & 0xFFFFULL);
                std::string s; s.resize(2);
                s[0] = static_cast<char>(v & 0xFF);
                s[1] = static_cast<char>((v >> 8) & 0xFF);
                stack.push_back(s);
                break;
            }
            case OpCode::OP_PACK_U32LE: {
                auto nAny = pop(stack);
                if (nAny.type() != typeid(double)) { stack.push_back(""s); break; }
                std::uint32_t v = static_cast<std::uint32_t>(static_cast<unsigned long long>(std::any_cast<double>(nAny)) & 0xFFFFFFFFULL);
                std::string s; s.resize(4);
                s[0] = static_cast<char>(v & 0xFF);
                s[1] = static_cast<char>((v >> 8) & 0xFF);
                s[2] = static_cast<char>((v >> 16) & 0xFF);
                s[3] = static_cast<char>((v >> 24) & 0xFF);
                stack.push_back(s);
                break;
            }
            case OpCode::OP_ASSERT: {
                auto cond = pop(stack);
                bool ok = isTruthy(cond);
                if (!ok) { std::cerr << "assertion failed\n"; return 1; }
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_PANIC: {
                auto msgAny = pop(stack);
                std::string msg = (msgAny.type() == typeid(std::string)) ? std::any_cast<std::string>(msgAny) : std::string("panic");
                std::cerr << msg << "\n";
                return 1;
            }
            case OpCode::OP_PARSE_INT: {
                auto sAny = pop(stack);
                if (sAny.type()!=typeid(std::string)) { stack.push_back(0.0); break; }
                try { long long v = std::stoll(std::any_cast<std::string>(sAny), nullptr, 10); stack.push_back(static_cast<double>(v)); }
                catch (...) { stack.push_back(0.0); }
                break;
            }
            case OpCode::OP_PARSE_FLOAT: {
                auto sAny = pop(stack);
                if (sAny.type()!=typeid(std::string)) { stack.push_back(0.0); break; }
                try { double v = std::stod(std::any_cast<std::string>(sAny)); stack.push_back(v); }
                catch (...) { stack.push_back(0.0); }
                break;
            }
            case OpCode::OP_TO_STRING: {
                auto v = pop(stack);
                stack.push_back(toString(v));
                break;
            }
            case OpCode::OP_FLOOR: { double a = popNum(stack); stack.push_back(std::floor(a)); break; }
            case OpCode::OP_CEIL: { double a = popNum(stack); stack.push_back(std::ceil(a)); break; }
            case OpCode::OP_ROUND: { double a = popNum(stack); stack.push_back(std::round(a)); break; }
            case OpCode::OP_SQRT: { double a = popNum(stack); stack.push_back(std::sqrt(std::max(0.0, a))); break; }
            case OpCode::OP_ABS: { double a = popNum(stack); stack.push_back(std::fabs(a)); break; }
            case OpCode::OP_POW: { double b = popNum(stack); double a = popNum(stack); stack.push_back(std::pow(a, b)); break; }
            case OpCode::OP_BAND: { double b = popNum(stack); double a = popNum(stack); stack.push_back(static_cast<double>((static_cast<std::int64_t>(a)) & (static_cast<std::int64_t>(b)))); break; }
            case OpCode::OP_BOR: { double b = popNum(stack); double a = popNum(stack); stack.push_back(static_cast<double>((static_cast<std::int64_t>(a)) | (static_cast<std::int64_t>(b)))); break; }
            case OpCode::OP_BXOR: { double b = popNum(stack); double a = popNum(stack); stack.push_back(static_cast<double>((static_cast<std::int64_t>(a)) ^ (static_cast<std::int64_t>(b)))); break; }
            case OpCode::OP_SHL: { double b = popNum(stack); double a = popNum(stack); stack.push_back(static_cast<double>((static_cast<std::int64_t>(a)) << (static_cast<std::int64_t>(b)))); break; }
            case OpCode::OP_SHR: { double b = popNum(stack); double a = popNum(stack); stack.push_back(static_cast<double>((static_cast<std::int64_t>(a)) >> (static_cast<std::int64_t>(b)))); break; }
            case OpCode::OP_ARRAY_RESERVE: {
                auto n = popNum(stack);
                auto anyArr = pop(stack);
                if (anyArr.type() != typeid(std::shared_ptr<std::vector<std::any>>)) { stack.push_back(false); break; }
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(anyArr);
                arr->reserve(static_cast<size_t>(std::max(0.0, n)));
                stack.push_back(true);
                break;
            }
            case OpCode::OP_ARRAY_CLEAR: {
                auto anyArr = pop(stack);
                if (anyArr.type() != typeid(std::shared_ptr<std::vector<std::any>>)) { stack.push_back(false); break; }
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(anyArr);
                arr->clear();
                stack.push_back(true);
                break;
            }
            case OpCode::OP_MAP_SIZE: {
                auto anyMap = pop(stack);
                if (anyMap.type() != typeid(std::shared_ptr<Map>)) { stack.push_back(0.0); break; }
                auto m = std::any_cast<std::shared_ptr<Map>>(anyMap);
                stack.push_back(static_cast<double>(m->size()));
                break;
            }
            case OpCode::OP_MAP_CLEAR: {
                auto anyMap = pop(stack);
                if (anyMap.type() != typeid(std::shared_ptr<Map>)) { stack.push_back(false); break; }
                auto m = std::any_cast<std::shared_ptr<Map>>(anyMap);
                m->clear();
                stack.push_back(true);
                break;
            }
            case OpCode::OP_EXIT: {
                auto codeAny = pop(stack);
                int code = (codeAny.type()==typeid(double)) ? static_cast<int>(std::any_cast<double>(codeAny)) : 0;
                stack.clear(); frames.clear(); globals.clear(); heap.clear();
                std::exit(code);
            }
            case OpCode::OP_STARTS_WITH: {
                auto s2Any = pop(stack);
                auto s1Any = pop(stack);
                if (s1Any.type()!=typeid(std::string) || s2Any.type()!=typeid(std::string)) { stack.push_back(false); break; }
                const std::string& s = std::any_cast<std::string>(s1Any);
                const std::string& p = std::any_cast<std::string>(s2Any);
                stack.push_back(s.rfind(p, 0) == 0);
                break;
            }
            case OpCode::OP_ENDS_WITH: {
                auto s2Any = pop(stack);
                auto s1Any = pop(stack);
                if (s1Any.type()!=typeid(std::string) || s2Any.type()!=typeid(std::string)) { stack.push_back(false); break; }
                const std::string& s = std::any_cast<std::string>(s1Any);
                const std::string& suf = std::any_cast<std::string>(s2Any);
                if (suf.size() > s.size()) { stack.push_back(false); break; }
                stack.push_back(std::equal(suf.rbegin(), suf.rend(), s.rbegin()));
                break;
            }
            case OpCode::OP_MAP_DEL: {
                auto keyAny = pop(stack);
                auto anyMap = pop(stack);
                if (anyMap.type() != typeid(std::shared_ptr<Map>) || keyAny.type() != typeid(std::string)) { stack.push_back(false); break; }
                auto m = std::any_cast<std::shared_ptr<Map>>(anyMap);
                const std::string& k = std::any_cast<std::string>(keyAny);
                size_t erased = m->erase(k);
                stack.push_back(erased>0);
                break;
            }
            case OpCode::OP_MAP_KEYS: {
                auto anyMap = pop(stack);
                if (anyMap.type() != typeid(std::shared_ptr<Map>)) { stack.push_back(nullptr); break; }
                auto m = std::any_cast<std::shared_ptr<Map>>(anyMap);
                auto arr = makeArray();
                arr->reserve(m->size());
                for (const auto& [k, _] : *m) arr->push_back(k);
                stack.push_back(arr);
                break;
            }
            case OpCode::OP_FILE_EXISTS: {
                auto pathAny = pop(stack);
                if (pathAny.type()!=typeid(std::string)) { stack.push_back(false); break; }
                const std::string& p = std::any_cast<std::string>(pathAny);
                std::ifstream ifs(p, std::ios::binary);
                stack.push_back((bool)ifs);
                break;
            }
            case OpCode::OP_ARRAY_POP: {
                auto anyArr = pop(stack);
                if (anyArr.type() != typeid(std::shared_ptr<std::vector<std::any>>)) { stack.push_back(nullptr); break; }
                auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(anyArr);
                if (!arr->empty()) arr->pop_back();
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_ORD: {
                auto sAny = pop(stack);
                if (sAny.type() != typeid(std::string) || std::any_cast<std::string>(sAny).empty()) { stack.push_back(0.0); break; }
                stack.push_back(static_cast<double>(static_cast<unsigned char>(std::any_cast<std::string>(sAny)[0])));
                break;
            }
            case OpCode::OP_CHR: {
                double n = popNum(stack);
                char c = static_cast<char>(static_cast<int>(n) & 0xFF);
                stack.push_back(std::string(1, c));
                break;
            }
            case OpCode::OP_READ_FILE: {
                auto pathAny = pop(stack);
                if (pathAny.type() != typeid(std::string)) { stack.push_back(""s); break; }
                const std::string& path = std::any_cast<std::string>(pathAny);
                std::ifstream ifs(path, std::ios::binary);
                if (!ifs) { stack.push_back(""s); break; }
                std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                stack.push_back(data);
                break;
            }
            case OpCode::OP_WRITE_FILE: {
                auto dataAny = pop(stack);
                auto pathAny = pop(stack);
                if (pathAny.type() != typeid(std::string) || dataAny.type() != typeid(std::string)) { stack.push_back(false); break; }
                const std::string& path = std::any_cast<std::string>(pathAny);
                const std::string& data = std::any_cast<std::string>(dataAny);
                std::ofstream ofs(path, std::ios::binary);
                bool ok = false; if (ofs) { ofs.write(data.data(), (std::streamsize)data.size()); ok = ofs.good(); }
                stack.push_back(ok);
                break;
            }
            // Streams
            case OpCode::OP_STDIN: { stack.push_back(static_cast<double>(0)); break; }
            case OpCode::OP_STDOUT: { stack.push_back(static_cast<double>(1)); break; }
            case OpCode::OP_STDERR: { stack.push_back(static_cast<double>(2)); break; }
            case OpCode::OP_FOPEN: {
                auto modeAny = pop(stack);
                auto pathAny = pop(stack);
                if (pathAny.type()!=typeid(std::string) || modeAny.type()!=typeid(std::string)) { stack.push_back(0.0); break; }
                const std::string& p = std::any_cast<std::string>(pathAny);
                const std::string& m = std::any_cast<std::string>(modeAny);
                std::ios::openmode om = std::ios::binary;
                if (m.find('r')!=std::string::npos) om |= std::ios::in;
                if (m.find('w')!=std::string::npos) om |= std::ios::out | std::ios::trunc;
                if (m.find('a')!=std::string::npos) om |= std::ios::out | std::ios::app;
                auto fs = std::make_shared<std::fstream>(p, om);
                if (!fs->is_open()) { stack.push_back(0.0); break; }
                int h = nextHandle++;
                streams[h] = fs;
                stack.push_back(static_cast<double>(h));
                break;
            }
            case OpCode::OP_FCLOSE: {
                auto hAny = pop(stack);
                if (hAny.type()!=typeid(double)) { stack.push_back(false); break; }
                int h = static_cast<int>(std::any_cast<double>(hAny));
                auto it = streams.find(h);
                if (it!=streams.end()) { it->second->close(); streams.erase(it); stack.push_back(true); }
                else { stack.push_back(false); }
                break;
            }
            case OpCode::OP_FREAD: {
                auto nAny = pop(stack);
                auto hAny = pop(stack);
                if (hAny.type()!=typeid(double) || nAny.type()!=typeid(double)) { stack.push_back(""s); break; }
                int h = static_cast<int>(std::any_cast<double>(hAny));
                size_t n = static_cast<size_t>(std::max(0.0, std::any_cast<double>(nAny)));
                if (h==0) { std::string s; s.resize(n); std::cin.read(&s[0], (std::streamsize)n); s.resize((size_t)std::cin.gcount()); stack.push_back(s); break; }
                auto it = streams.find(h);
                if (it==streams.end()) { stack.push_back(""s); break; }
                std::string s; s.resize(n);
                it->second->read(&s[0], (std::streamsize)n);
                s.resize((size_t)it->second->gcount());
                stack.push_back(s);
                break;
            }
            case OpCode::OP_FREADLINE: {
                auto hAny = pop(stack);
                if (hAny.type()!=typeid(double)) { stack.push_back(""s); break; }
                int h = static_cast<int>(std::any_cast<double>(hAny));
                std::string s;
                if (h==0) { std::getline(std::cin, s); stack.push_back(s); break; }
                auto it = streams.find(h);
                if (it==streams.end()) { stack.push_back(""s); break; }
                std::getline(*it->second, s);
                stack.push_back(s);
                break;
            }
            case OpCode::OP_FWRITE: {
                auto dataAny = pop(stack);
                auto hAny = pop(stack);
                if (hAny.type()!=typeid(double) || dataAny.type()!=typeid(std::string)) { stack.push_back(false); break; }
                int h = static_cast<int>(std::any_cast<double>(hAny));
                const std::string& data = std::any_cast<std::string>(dataAny);
                if (h==1) { std::cout << data; stack.push_back(true); break; }
                if (h==2) { std::cerr << data; stack.push_back(true); break; }
                auto it = streams.find(h);
                if (it==streams.end()) { stack.push_back(false); break; }
                (*it->second) << data;
                stack.push_back(it->second->good());
                break;
            }
            case OpCode::OP_ALLOC: {
                size_t n = static_cast<size_t>(std::max(0.0, popNum(stack)));
                auto b = std::make_shared<Block>(); b->data.assign(n, 0);
                int idx = static_cast<int>(heap.size()); heap.push_back(b);
                auto p = std::make_shared<Pointer>(); p->block = idx; p->offset = 0;
                stack.push_back(p);
                break;
            }
            case OpCode::OP_FREE: {
                auto anyP = pop(stack);
                if (anyP.type() != typeid(std::shared_ptr<Pointer>)) { stack.push_back(nullptr); break; }
                auto p = std::any_cast<std::shared_ptr<Pointer>>(anyP);
                if (p->block < 0 || p->block >= (int)heap.size() || !heap[p->block] || heap[p->block]->freed) { stack.push_back(nullptr); break; }
                heap[p->block]->freed = true; heap[p->block]->data.clear(); heap[p->block]->data.shrink_to_fit();
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_PTR_ADD: {
                double off = popNum(stack);
                auto anyP = pop(stack);
                if (anyP.type() != typeid(std::shared_ptr<Pointer>)) { stack.push_back(nullptr); break; }
                auto p = std::any_cast<std::shared_ptr<Pointer>>(anyP);
                auto np = std::make_shared<Pointer>(*p);
                long long delta = static_cast<long long>(off);
                if (delta < 0 && static_cast<size_t>(-delta) > np->offset) np->offset = 0; else np->offset += static_cast<size_t>(delta);
                stack.push_back(np);
                break;
            }
            case OpCode::OP_LOAD8: {
                double off = popNum(stack);
                auto anyP = pop(stack);
                if (anyP.type() != typeid(std::shared_ptr<Pointer>)) { stack.push_back(0.0); break; }
                auto p = std::any_cast<std::shared_ptr<Pointer>>(anyP);
                if (p->block < 0 || p->block >= (int)heap.size() || !heap[p->block] || heap[p->block]->freed) { stack.push_back(0.0); break; }
                auto& data = heap[p->block]->data;
                size_t idx = p->offset + static_cast<size_t>(off);
                if (idx >= data.size()) { stack.push_back(0.0); break; }
                stack.push_back(static_cast<double>(data[idx]));
                break;
            }
            case OpCode::OP_STORE8: {
                double off = popNum(stack);
                auto anyP = pop(stack);
                double val = popNum(stack);
                if (anyP.type() != typeid(std::shared_ptr<Pointer>)) { stack.push_back(nullptr); break; }
                auto p = std::any_cast<std::shared_ptr<Pointer>>(anyP);
                if (p->block < 0 || p->block >= (int)heap.size() || !heap[p->block] || heap[p->block]->freed) { stack.push_back(nullptr); break; }
                auto& data = heap[p->block]->data;
                size_t idx = p->offset + static_cast<size_t>(off);
                if (idx < data.size()) data[idx] = static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, val)));
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_LOAD32: {
                double off = popNum(stack);
                auto anyP = pop(stack);
                if (anyP.type() != typeid(std::shared_ptr<Pointer>)) { stack.push_back(0.0); break; }
                auto p = std::any_cast<std::shared_ptr<Pointer>>(anyP);
                if (p->block < 0 || p->block >= (int)heap.size() || !heap[p->block] || heap[p->block]->freed) { stack.push_back(0.0); break; }
                auto& data = heap[p->block]->data;
                size_t idx = p->offset + static_cast<size_t>(off);
                if (idx + 4 > data.size()) { stack.push_back(0.0); break; }
                std::uint32_t v = data[idx] | (data[idx+1]<<8) | (data[idx+2]<<16) | (data[idx+3]<<24);
                stack.push_back(static_cast<double>(v));
                break;
            }
            case OpCode::OP_STORE32: {
                double off = popNum(stack);
                auto anyP = pop(stack);
                double v = popNum(stack);
                if (anyP.type() != typeid(std::shared_ptr<Pointer>)) { stack.push_back(nullptr); break; }
                auto p = std::any_cast<std::shared_ptr<Pointer>>(anyP);
                if (p->block < 0 || p->block >= (int)heap.size() || !heap[p->block] || heap[p->block]->freed) { stack.push_back(nullptr); break; }
                auto& data = heap[p->block]->data;
                size_t idx = p->offset + static_cast<size_t>(off);
                std::uint32_t val = static_cast<std::uint32_t>(v);
                if (idx + 4 <= data.size()) {
                    data[idx] = static_cast<std::uint8_t>(val & 0xFF);
                    data[idx+1] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
                    data[idx+2] = static_cast<std::uint8_t>((val >> 16) & 0xFF);
                    data[idx+3] = static_cast<std::uint8_t>((val >> 24) & 0xFF);
                }
                stack.push_back(nullptr);
                break;
            }
            case OpCode::OP_POP: { stack.pop_back(); break; }
            case OpCode::OP_CALL: {
                std::uint16_t ni = readU16(pc);
                std::uint8_t argc = chunk.code[pc++];
                const std::string& name = chunk.names[ni];
                // resolve function entry
                std::uint32_t entry = 0xFFFFFFFF;
                std::uint16_t arity = 0;
                for (const auto& f : chunk.functions) {
                    if (chunk.names[f.nameIndex] == name) { entry = f.entry; arity = f.arity; break; }
                }
                if (entry == 0xFFFFFFFF) { reportRuntimeError(std::string("Undefined function: ") + name); return 1; }
                if (argc != arity) { reportRuntimeError(std::string("Arity mismatch for ") + name); return 1; }
                // naive call: push return address marker then jump
                // collect args into new frame locals [0..argc)
                Frame fr; fr.returnPC = pc; fr.locals.resize(argc);
                for (int i = argc - 1; i >= 0; --i) { fr.locals[i] = stack.back(); stack.pop_back(); }
                frames.push_back(std::move(fr));
                pc = entry;
                break;
            }
            case OpCode::OP_RETURN: {
                if (frames.empty()) return 0;
                std::uint32_t ret = frames.back().returnPC;
                frames.pop_back();
                pc = ret;
                break;
            }
            case OpCode::OP_HALT: {
                // release runtime state to help leak detectors and free memory early
                stack.clear();
                frames.clear();
                globals.clear();
                heap.clear();
                if (memDbg) {
                    std::cerr << "[mem] arrays: " << arraysCreated << "/" << arraysDestroyed << ", maps: " << mapsCreated << "/" << mapsDestroyed << "\n";
                }
                return 0;
            }
            case OpCode::OP_EMIT_CHUNK: {
                auto pathAny = pop(stack);
                auto manifestAny = pop(stack);
                if (pathAny.type()!=typeid(std::string) || manifestAny.type()!=typeid(std::shared_ptr<std::unordered_map<std::string, std::any>>)) { stack.push_back(false); break; }
                const std::string& outPath = std::any_cast<std::string>(pathAny);
                using Map = std::unordered_map<std::string, std::any>;
                auto m = std::any_cast<std::shared_ptr<Map>>(manifestAny);
                Chunk out;
                auto getArr = [&](const std::string& key)->std::shared_ptr<std::vector<std::any>>{
                    auto it = m->find(key); if (it==m->end()) return {};
                    if (it->second.type()!=typeid(std::shared_ptr<std::vector<std::any>>)) return {};
                    return std::any_cast<std::shared_ptr<std::vector<std::any>>>(it->second);
                };
                if (auto carr = getArr("constants")) { for (const auto& v : *carr) { if (!v.has_value() || v.type()==typeid(std::nullptr_t)) out.constants.push_back(std::monostate{}); else if (v.type()==typeid(double)) out.constants.push_back(std::any_cast<double>(v)); else if (v.type()==typeid(std::string)) out.constants.push_back(std::any_cast<std::string>(v)); else if (v.type()==typeid(bool)) out.constants.push_back(std::any_cast<bool>(v)); } }
                if (auto narr = getArr("names")) { for (const auto& v : *narr) if (v.type()==typeid(std::string)) out.names.push_back(std::any_cast<std::string>(v)); }
                if (auto farr = getArr("functions")) {
                    for (const auto& fnAny : *farr) {
                        if (fnAny.type()!=typeid(std::shared_ptr<Map>)) continue;
                        auto fm = std::any_cast<std::shared_ptr<Map>>(fnAny);
                        std::uint16_t nameIdx = 0, ar = 0; std::uint32_t ent = 0;
                        auto fnd = fm->find("nameIndex"); if (fnd!=fm->end() && fnd->second.type()==typeid(double)) nameIdx = static_cast<std::uint16_t>(std::any_cast<double>(fnd->second));
                        fnd = fm->find("arity"); if (fnd!=fm->end() && fnd->second.type()==typeid(double)) ar = static_cast<std::uint16_t>(std::any_cast<double>(fnd->second));
                        fnd = fm->find("entry"); if (fnd!=fm->end() && fnd->second.type()==typeid(double)) ent = static_cast<std::uint32_t>(std::any_cast<double>(fnd->second));
                        out.functions.push_back({ nameIdx, ar, ent });
                    }
                }
                {
                    auto it = m->find("code"); if (it!=m->end() && it->second.type()==typeid(std::string)) { const std::string& raw = std::any_cast<std::string>(it->second); out.code.assign(raw.begin(), raw.end()); }
                }
                if (auto dl = getArr("debugLines")) { for (const auto& v : *dl) if (v.type()==typeid(double)) out.debugLines.push_back(static_cast<std::uint32_t>(std::any_cast<double>(v))); }
                if (auto dc = getArr("debugCols")) { for (const auto& v : *dc) if (v.type()==typeid(double)) out.debugCols.push_back(static_cast<std::uint32_t>(std::any_cast<double>(v))); }
                std::ofstream ofs(outPath, std::ios::binary);
                if (!ofs.is_open()) { stack.push_back(false); break; }
                writeChunk(out, ofs);
                ofs.flush();
                stack.push_back(ofs.good());
                break;
            }
            default: return 1;
            }
        }
        if (memDbg) {
            std::cerr << "[mem] arrays: " << arraysCreated << "/" << arraysDestroyed << ", maps: " << mapsCreated << "/" << mapsDestroyed << "\n";
        }
        return 0;
    }

private:
    std::vector<std::uint32_t> callStack;
    static double popNum(std::vector<std::any>& s) {
        double v = std::any_cast<double>(s.back()); s.pop_back(); return v;
    }
    static std::any pop(std::vector<std::any>& s) { auto v = s.back(); s.pop_back(); return v; }
    template<typename F>
    static void binary(std::vector<std::any>& s, F f) {
        double b = popNum(s); double a = popNum(s); s.push_back(f(a,b));
    }
    template<typename P>
    static void cmp(std::vector<std::any>& s, P p) {
        double b = popNum(s); double a = popNum(s); s.push_back(p(a,b));
    }
    static void beq(std::vector<std::any>& s, bool eq) {
        std::any rb = s.back(); s.pop_back();
        std::any lb = s.back(); s.pop_back();
        bool r = equals(lb, rb);
        s.push_back(eq ? r : !r);
    }
    static bool equals(const std::any& a, const std::any& b) {
        if (a.type() == typeid(std::nullptr_t) && b.type() == typeid(std::nullptr_t)) return true;
        if (a.type() == typeid(double) && b.type() == typeid(double)) return std::any_cast<double>(a) == std::any_cast<double>(b);
        if (a.type() == typeid(bool) && b.type() == typeid(bool)) return std::any_cast<bool>(a) == std::any_cast<bool>(b);
        if (a.type() == typeid(std::string) && b.type() == typeid(std::string)) return std::any_cast<std::string>(a) == std::any_cast<std::string>(b);
        return false;
    }
    static void write(const std::any& v) {
        if (!v.has_value() || v.type() == typeid(std::nullptr_t)) { /* print nothing for nil */ return; }
        if (v.type() == typeid(bool)) { std::cout << (std::any_cast<bool>(v) ? "true" : "false"); return; }
        if (v.type() == typeid(double)) {
            double d = std::any_cast<double>(v);
            double tr = std::trunc(d);
            if (std::fabs(d - tr) < 1e-12) { std::cout << static_cast<long long>(tr); }
            else { std::cout << d; }
            return;
        }
        if (v.type() == typeid(std::string)) { std::cout << std::any_cast<std::string>(v); return; }
        if (v.type() == typeid(std::shared_ptr<std::vector<std::any>>)) {
            auto arr = std::any_cast<std::shared_ptr<std::vector<std::any>>>(v);
            std::cout << "[";
            for (size_t i = 0; i < arr->size(); ++i) { write((*arr)[i]); if (i + 1 < arr->size()) std::cout << ", "; }
            std::cout << "]"; return;
        }
        std::cout << "<val>";
    }
    static bool isTruthy(const std::any& v) {
        if (!v.has_value() || v.type() == typeid(std::nullptr_t)) return false;
        if (v.type() == typeid(bool)) return std::any_cast<bool>(v);
        if (v.type() == typeid(double)) return std::any_cast<double>(v) != 0.0;
        return true;
    }
};


