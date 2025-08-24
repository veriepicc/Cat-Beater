export module IR;

import <cstdint>;
import <vector>;
import <string>;
import <variant>;

export using Value = std::variant<std::monostate, double, std::string, bool>;

export enum class OpCode : std::uint8_t {
    OP_CONST,      // u16 const index
    OP_GET_GLOBAL, // u16 name index
    OP_SET_GLOBAL, // u16 name index
    OP_JUMP,       // u16 forward offset
    OP_JUMP_IF_FALSE, // u16 forward offset (consumes top but leaves it; caller usually OP_POPs)
    OP_LOOP,       // u16 backward offset
    OP_CALL,       // u16 name index, u8 argc
    OP_RETURN,     // (returns top of stack or nil)
    OP_GET_LOCAL,  // u16 index
    OP_SET_LOCAL,  // u16 index
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_GT,
    OP_GE,
    OP_LT,
    OP_LE,
    OP_EQ,
    OP_NE,
    OP_AND,
    OP_OR,
    OP_NEW_ARRAY,  // u8 count (elements are on stack)
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_LEN,
    OP_APPEND,
    OP_ALLOC,     // expects size on stack
    OP_FREE,      // expects ptr on stack
    OP_PTR_ADD,   // expects ptr, offset on stack
    OP_LOAD8,     // expects ptr, offset
    OP_STORE8,    // expects value, ptr, offset
    OP_LOAD32,    // expects ptr, offset
    OP_STORE32,   // expects value, ptr, offset
    OP_NEW_MAP,
    OP_MAP_GET,
    OP_MAP_SET,
    OP_MAP_HAS,
    OP_STR_INDEX,
    OP_SUBSTR,
    OP_STR_FIND,
    OP_SPLIT,
    OP_STR_CAT,
    OP_JOIN,
    OP_TRIM,
    OP_REPLACE,
    OP_ARRAY_POP,
    OP_PACK_F64LE,
    OP_PACK_U16LE,
    OP_PACK_U32LE,
    OP_ASSERT,
    OP_PANIC,
    OP_PARSE_INT,
    OP_PARSE_FLOAT,
    OP_STARTS_WITH,
    OP_ENDS_WITH,
    OP_MAP_DEL,
    OP_MAP_KEYS,
    OP_FILE_EXISTS,
    OP_TO_STRING,
    OP_FLOOR,
    OP_CEIL,
    OP_ROUND,
    OP_SQRT,
    OP_ABS,
    OP_POW,
    // Extended math
    OP_EXP,
    OP_LOG,
    OP_SIN,
    OP_COS,
    OP_TAN,
    OP_ASIN,
    OP_ACOS,
    OP_ATAN,
    OP_ATAN2,
    OP_RANDOM,
    OP_BAND,
    OP_BOR,
    OP_BXOR,
    OP_SHL,
    OP_SHR,
    OP_ARRAY_RESERVE,
    OP_ARRAY_CLEAR,
    OP_MAP_SIZE,
    OP_MAP_CLEAR,
    OP_EXIT,
    OP_ORD,
    OP_CHR,
    OP_READ_FILE,
    OP_WRITE_FILE,
    OP_PRINT,      // u8 count
    OP_POP,
    OP_HALT,
    // Extended memory and pointer ops (native runtime)
    OP_LOAD16,
    OP_STORE16,
    OP_LOAD64,
    OP_STORE64,
    OP_LOADF32,
    OP_STOREF32,
    OP_MEMCPY,
    OP_MEMSET,
    OP_PTR_DIFF,
    OP_REALLOC,
    OP_BLOCK_SIZE,
    OP_PTR_OFFSET,
    OP_PTR_BLOCK,
    // Rich strings
    OP_STR_UPPER,
    OP_STR_LOWER,
    OP_STR_CONTAINS,
    OP_FORMAT,     // u8 count (fmt + N args)
    // Stream I/O
    OP_FOPEN,      // expects path, mode
    OP_FCLOSE,     // expects handle
    OP_FREAD,      // expects handle, n -> string
    OP_FWRITE,     // expects handle, data -> bool
    OP_FREADLINE,  // expects handle -> string
    OP_STDIN,
    OP_STDOUT,
    OP_STDERR,
    // Chunk emission for self-host
    OP_EMIT_CHUNK,  // expects manifest(map) and path(string); returns bool
    // Opcode id helper
    OP_OPCODE_ID,   // expects string name; returns numeric id (u8) as number
    // Foreign function interface
    OP_FFI_CALL,     // u8 argc; expects: argN..arg1, funcName(string), dllName(string)
    OP_FFI_CALL_SIG  // u8 argc; expects: argN..arg1, dllName(string), funcName(string), signature(string)
    ,OP_FFI_PROC     // expects: funcName(string or number=ordinal), dllName(string); pushes pointer (u64 as number)
    ,OP_FFI_CALL_PTR // u8 argc; expects: argN..arg1, ptr(number), signature(string)
    ,OP_CALLN_ARR    // expects: args(array), funcName(string)
};

export struct Chunk {
    std::vector<Value> constants;
    std::vector<std::string> names;
    std::vector<std::uint8_t> code;
    std::vector<std::uint32_t> debugLines; // one entry per code byte (same size as code when populated)
    std::vector<std::uint32_t> debugCols;  // one entry per code byte (optional)
    std::string sourceName;
    struct FunctionInfo { std::uint16_t nameIndex; std::uint16_t arity; std::uint32_t entry; };
    std::vector<FunctionInfo> functions;

    std::uint16_t addConstant(const Value& v) {
        constants.push_back(v);
        return static_cast<std::uint16_t>(constants.size() - 1);
    }
    std::uint16_t addName(const std::string& s) {
        names.push_back(s);
        return static_cast<std::uint16_t>(names.size() - 1);
    }

    void writeOp(OpCode op) { code.push_back(static_cast<std::uint8_t>(op)); }
    void writeByte(std::uint8_t b) { code.push_back(b); }
    void writeU16(std::uint16_t v) {
        code.push_back(static_cast<std::uint8_t>(v & 0xFF));
        code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }
    void writeU32(std::uint32_t v) {
        code.push_back(static_cast<std::uint8_t>(v & 0xFF));
        code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        code.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        code.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    }

    // Debug-aware writers: also append line info per byte
    void writeOpL(OpCode op, std::uint32_t line) {
        writeOp(op);
        if (debugLines.size() < code.size()) debugLines.resize(code.size()-1);
        debugLines.push_back(line);
    }
    void writeByteL(std::uint8_t b, std::uint32_t line) {
        writeByte(b);
        debugLines.push_back(line);
    }
    void writeU16L(std::uint16_t v, std::uint32_t line) {
        writeByteL(static_cast<std::uint8_t>(v & 0xFF), line);
        writeByteL(static_cast<std::uint8_t>((v >> 8) & 0xFF), line);
    }
};


