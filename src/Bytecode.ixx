export module Bytecode;

import IR;
import <ostream>;
import <istream>;
import <vector>;
import <string>;
import <cstdint>;
import <stdexcept>;
import <type_traits>;
import <variant>;

namespace {
    constexpr std::uint32_t MAGIC = 0x43424243; // 'CBBC'
    constexpr std::uint16_t VERSION = 1;
}

export void writeChunk(const Chunk& chunk, std::ostream& os) {
    auto writeU32 = [&](std::uint32_t v){ os.put(static_cast<char>(v & 0xFF)); os.put(static_cast<char>((v>>8)&0xFF)); os.put(static_cast<char>((v>>16)&0xFF)); os.put(static_cast<char>((v>>24)&0xFF)); };
    auto writeU16 = [&](std::uint16_t v){ os.put(static_cast<char>(v & 0xFF)); os.put(static_cast<char>((v>>8)&0xFF)); };
    auto writeStr = [&](const std::string& s){ writeU32(static_cast<std::uint32_t>(s.size())); os.write(s.data(), static_cast<std::streamsize>(s.size())); };

    writeU32(MAGIC);
    writeU16(VERSION);

    // constants
    writeU32(static_cast<std::uint32_t>(chunk.constants.size()));
    for (const auto& v : chunk.constants) {
        if (std::holds_alternative<std::monostate>(v)) {
            os.put(static_cast<char>(0));
        } else if (std::holds_alternative<double>(v)) {
            os.put(static_cast<char>(1));
            double d = std::get<double>(v);
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&d);
            for (int i = 0; i < 8; ++i) os.put(static_cast<char>(p[i]));
        } else if (std::holds_alternative<std::string>(v)) {
            os.put(static_cast<char>(2));
            writeStr(std::get<std::string>(v));
        } else if (std::holds_alternative<bool>(v)) {
            os.put(static_cast<char>(3));
            os.put(static_cast<char>(std::get<bool>(v) ? 1 : 0));
        } else {
            throw std::runtime_error("Unknown constant type");
        }
    }

    // names
    writeU32(static_cast<std::uint32_t>(chunk.names.size()));
    for (const auto& s : chunk.names) writeStr(s);

    // functions table
    writeU32(static_cast<std::uint32_t>(chunk.functions.size()));
    for (const auto& f : chunk.functions) {
        writeU16(f.nameIndex);
        writeU16(f.arity);
        writeU32(f.entry);
    }

    // code
    writeU32(static_cast<std::uint32_t>(chunk.code.size()));
    if (!chunk.code.empty()) os.write(reinterpret_cast<const char*>(chunk.code.data()), static_cast<std::streamsize>(chunk.code.size()));
    // debug lines (optional, partial allowed)
    writeU32(static_cast<std::uint32_t>(chunk.debugLines.size()));
    for (auto ln : chunk.debugLines) writeU32(ln);
    // debug columns (optional)
    writeU32(static_cast<std::uint32_t>(chunk.debugCols.size()));
    for (auto cl : chunk.debugCols) writeU32(cl);
}

export Chunk loadChunk(std::istream& is) {
    auto readU32 = [&](){ std::uint32_t b0 = static_cast<std::uint8_t>(is.get()); std::uint32_t b1 = static_cast<std::uint8_t>(is.get()); std::uint32_t b2 = static_cast<std::uint8_t>(is.get()); std::uint32_t b3 = static_cast<std::uint8_t>(is.get()); return static_cast<std::uint32_t>(b0 | (b1<<8) | (b2<<16) | (b3<<24)); };
    auto readU16 = [&](){ std::uint16_t b0 = static_cast<std::uint8_t>(is.get()); std::uint16_t b1 = static_cast<std::uint8_t>(is.get()); return static_cast<std::uint16_t>(b0 | (b1<<8)); };
    auto readStr = [&](){ std::uint32_t n = readU32(); std::string s; s.resize(n); if (n) is.read(&s[0], static_cast<std::streamsize>(n)); return s; };

    Chunk chunk;
    std::uint32_t magic = readU32();
    if (magic != MAGIC) throw std::runtime_error("Invalid bytecode magic");
    std::uint16_t ver = readU16(); (void)ver;

    // constants
    std::uint32_t ccount = readU32();
    chunk.constants.reserve(ccount);
    for (std::uint32_t i=0;i<ccount;++i) {
        int tag = static_cast<std::uint8_t>(is.get());
        switch (tag) {
        case 0: chunk.constants.push_back(std::monostate{}); break;
        case 1: {
            double d = 0; unsigned char* p = reinterpret_cast<unsigned char*>(&d); for (int i=0;i<8;++i) p[i] = static_cast<unsigned char>(is.get()); chunk.constants.push_back(d); break; }
        case 2: chunk.constants.push_back(readStr()); break;
        case 3: chunk.constants.push_back(static_cast<std::uint8_t>(is.get()) != 0); break;
        default: throw std::runtime_error("Unknown const tag");
        }
    }

    // names
    std::uint32_t ncount = readU32();
    chunk.names.reserve(ncount);
    for (std::uint32_t i=0;i<ncount;++i) chunk.names.push_back(readStr());

    // functions table
    std::uint32_t fcount = readU32();
    chunk.functions.reserve(fcount);
    for (std::uint32_t i=0;i<fcount;++i) {
        std::uint16_t nameIndex = readU16();
        std::uint16_t arity = readU16();
        std::uint32_t entry = readU32();
        chunk.functions.push_back(Chunk::FunctionInfo{ nameIndex, arity, entry });
    }

    // code
    std::uint32_t clen = readU32();
    chunk.code.resize(clen);
    if (clen) is.read(reinterpret_cast<char*>(chunk.code.data()), static_cast<std::streamsize>(clen));
    // debug lines
    std::uint32_t dlen = readU32();
    if (dlen) {
        chunk.debugLines.resize(dlen);
        for (std::uint32_t i=0;i<dlen;++i) chunk.debugLines[i] = readU32();
    }
    // debug columns
    std::uint32_t clen2 = readU32();
    if (clen2) {
        chunk.debugCols.resize(clen2);
        for (std::uint32_t i=0;i<clen2;++i) chunk.debugCols[i] = readU32();
    }

    return chunk;
}


