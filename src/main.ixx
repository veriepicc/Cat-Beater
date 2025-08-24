import <iostream>;
import <fstream>;
import <sstream>;
import <string_view>;
import <cctype>;
import <vector>;
import <string>;
import <memory>;
import <cstdlib>;
import <filesystem>;
import <unordered_set>;
import <cstdint>;
import <cstring>;
// English-only mode
import Lexer;
import Parser;
import Stmt;
import IR;
import Codegen;
import Bytecode;
import JIT;
import Bytecode;
import NativeExec;
import FFI;

// --- Bundling helpers (EXE) ---
namespace {
    struct CBPackFooter { char magic[8]; std::uint64_t payloadSize; };
    static constexpr char CBPACK_MAGIC[8] = {'C','B','P','A','C','K','1','\0'};

    static std::string makeAbsolute(const std::string& pathLike) {
        std::error_code ec;
        auto p = std::filesystem::path(pathLike);
        if (!p.is_absolute()) p = std::filesystem::absolute(p, ec);
        return p.string();
    }

    static bool tryLoadEmbeddedFromModule(const std::string& modulePath, std::string& outBytes) {
        std::ifstream ifs(modulePath, std::ios::binary);
        if (!ifs.is_open()) return false;
        ifs.seekg(0, std::ios::end);
        std::streamoff fileSize = ifs.tellg();
        if (fileSize < static_cast<std::streamoff>(sizeof(CBPackFooter))) return false;
        ifs.seekg(fileSize - static_cast<std::streamoff>(sizeof(CBPackFooter)), std::ios::beg);
        CBPackFooter f{};
        ifs.read(reinterpret_cast<char*>(&f), static_cast<std::streamsize>(sizeof(f)));
        if (!ifs.good()) return false;
        if (std::memcmp(f.magic, CBPACK_MAGIC, sizeof(CBPACK_MAGIC)) != 0) return false;
        if (f.payloadSize == 0 || f.payloadSize > static_cast<std::uint64_t>(fileSize)) return false;
        std::streamoff payloadPos = fileSize - static_cast<std::streamoff>(sizeof(CBPackFooter)) - static_cast<std::streamoff>(f.payloadSize);
        if (payloadPos < 0) return false;
        ifs.seekg(payloadPos, std::ios::beg);
        outBytes.resize(static_cast<size_t>(f.payloadSize));
        if (f.payloadSize) ifs.read(&outBytes[0], static_cast<std::streamsize>(f.payloadSize));
        return ifs.good();
    }

    static bool writeBundledFile(const std::string& stubPath, const std::string& outPath, const Chunk& chunk) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(outPath).parent_path(), ec);
        std::filesystem::copy_file(stubPath, outPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { std::cerr << "bundle: failed to copy stub from '" << stubPath << "' to '" << outPath << "': " << ec.message() << std::endl; return false; }
        std::ofstream ofs(outPath, std::ios::binary | std::ios::app);
        if (!ofs.is_open()) { std::cerr << "bundle: could not open output for append: " << outPath << std::endl; return false; }
        std::ostringstream payload;
        writeChunk(chunk, payload);
        const std::string bytes = payload.str();
        ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        CBPackFooter f{};
        std::memcpy(f.magic, CBPACK_MAGIC, sizeof(CBPACK_MAGIC));
        f.payloadSize = static_cast<std::uint64_t>(bytes.size());
        ofs.write(reinterpret_cast<const char*>(&f), static_cast<std::streamsize>(sizeof(f)));
        ofs.flush();
        if (!ofs.good()) { std::cerr << "bundle: failed writing payload/footer." << std::endl; return false; }
        return true;
    }
}

// Simple helpers for suggestions and auto-fixes
static bool envFlag(const char* name) {
    bool on = false; char* envVal = nullptr; size_t envLen = 0;
#if defined(_MSC_VER)
    if (_dupenv_s(&envVal, &envLen, name) == 0 && envVal && envLen > 0 && envVal[0] != '0') on = true;
    if (envVal) { free(envVal); envVal = nullptr; }
#else
    const char* v = std::getenv(name); if (v && v[0] != '0') on = true;
#endif
    return on;
}

// Default-on variant: returns true unless the env var is set to '0'
static bool envFlagDefaultOn(const char* name) {
    bool on = true; char* envVal = nullptr; size_t envLen = 0;
#if defined(_MSC_VER)
    if (_dupenv_s(&envVal, &envLen, name) == 0) {
        if (envVal && envLen > 0 && envVal[0] == '0') on = false;
    }
    if (envVal) { free(envVal); envVal = nullptr; }
#else
    const char* v = std::getenv(name);
    if (v && v[0] == '0') on = false;
#endif
    return on;
}
struct Fix { std::string suggestion; std::string fixed; };
static std::string readAllText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::string();
    std::stringstream b; b << f.rdbuf();
    return b.str();
}

static std::string expandImports(const std::string& source, const std::filesystem::path& baseDir, std::unordered_set<std::string>& visiting) {
    std::stringstream out;
    std::istringstream iss(source);
    std::string line;
    auto ltrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin()); };
    auto rtrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); };
    while (std::getline(iss, line)) {
        std::string t = line; ltrim(t); rtrim(t);
        bool isImport = false;
        size_t pos = std::string::npos;
        if (t.rfind("use \"", 0) == 0) { isImport = true; pos = 4; }
        if (!isImport && t.rfind("import \"", 0) == 0) { isImport = true; pos = 7; }
        // Modern/English includes
        if (!isImport && t.rfind("include \"", 0) == 0) { isImport = true; pos = 8; }
        // C-style #include "path"
        if (!isImport && t.rfind("#include \"", 0) == 0) { isImport = true; pos = 10; }
        if (isImport) {
            size_t q1 = t.find('"', pos);
            size_t q2 = (q1 == std::string::npos) ? std::string::npos : t.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
                std::string rel = t.substr(q1 + 1, q2 - q1 - 1);
                std::filesystem::path child = baseDir / rel;
                std::error_code ec; auto canonical = std::filesystem::weakly_canonical(child, ec);
                std::string key = canonical.string();
                if (!key.empty() && visiting.insert(key).second) {
                    std::string childSrc = readAllText(key);
                    if (!childSrc.empty()) {
                        out << "/* begin import: " << key << " */\n";
                        out << expandImports(childSrc, canonical.parent_path(), visiting);
                        out << "\n/* end import: " << key << " */\n";
                        continue;
                    }
                }
            }
        }
        out << line << "\n";
    }
    return out.str();
}

// Map each physical line in the expanded source back to its originating file and local line number
struct LineOrigin { std::string filePath; std::uint32_t localLine; };
static std::vector<LineOrigin> buildLineOrigins(const std::string& expandedSource, const std::string& rootPath) {
    std::vector<LineOrigin> origins;
    struct Frame { std::string file; std::uint32_t line; };
    std::vector<Frame> stack;
    stack.push_back(Frame{ rootPath, 0 });

    std::istringstream iss(expandedSource);
    std::string line;
    auto ltrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin()); };
    auto rtrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); };
    while (std::getline(iss, line)) {
        std::string t = line; ltrim(t); rtrim(t);
        // Detect our include sentinels exactly as emitted by expandImports
        if (t.rfind("/* begin import: ", 0) == 0 && t.size() >= 4 && t.substr(t.size()-2) == "*/") {
            // Extract path between prefix and suffix
            std::string inside = t.substr(std::string("/* begin import: ").size());
            if (inside.size() >= 2 && inside.substr(inside.size()-2) == "*/") inside.resize(inside.size()-2);
            // Trim trailing space
            while (!inside.empty() && std::isspace((unsigned char)inside.back())) inside.pop_back();
            if (!inside.empty()) {
                stack.push_back(Frame{ inside, 0 });
            }
            // Record origin for this sentinel line as belonging to the new frame (line 0 -> will ++ below)
        } else if (t.rfind("/* end import: ", 0) == 0 && t.size() >= 4 && t.substr(t.size()-2) == "*/") {
            if (stack.size() > 1) stack.pop_back();
        }
        // Increment line in current frame and record origin
        if (!stack.empty()) {
            stack.back().line += 1;
            origins.push_back(LineOrigin{ stack.back().file, stack.back().line });
        } else {
            // Fallback to root if stack somehow empty
            origins.push_back(LineOrigin{ rootPath, static_cast<std::uint32_t>(origins.size()+1) });
        }
    }
    return origins;
}

// Count occurrences of a word (e.g., "end"/"do") outside of quotes and with balanced ()[]{} depth == 0
static int countWordOutsideQuotesAndBalanced(const std::string& s, const std::string& word) {
    int count = 0;
    bool inQuote = false;
    int paren = 0, bracket = 0, brace = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!inQuote) {
            if (c == '"') { inQuote = true; continue; }
            if (c == '(') { paren++; continue; }
            if (c == ')') { if (paren > 0) paren--; continue; }
            if (c == '[') { bracket++; continue; }
            if (c == ']') { if (bracket > 0) bracket--; continue; }
            if (c == '{') { brace++; continue; }
            if (c == '}') { if (brace > 0) brace--; continue; }
            if (paren == 0 && bracket == 0 && brace == 0) {
                // Try match word here
                if (c == word[0]) {
                    size_t remaining = s.size() - i;
                    if (remaining >= word.size() && s.compare(i, word.size(), word) == 0) {
                        // Check word boundaries: prev not [A-Za-z0-9_], next not [A-Za-z0-9_]
                        bool prevOk = (i == 0) || !(std::isalnum((unsigned char)s[i-1]) || s[i-1] == '_');
                        bool nextOk = (i + word.size() >= s.size()) || !(std::isalnum((unsigned char)s[i + word.size()]) || s[i + word.size()] == '_');
                        if (prevOk && nextOk) { count++; i += word.size() - 1; continue; }
                    }
                }
            }
        } else {
            if (c == '"') { inQuote = false; }
        }
    }
    return count;
}


static bool suggestFix(const std::string& chunk, Fix& out) {
    // operate on first line only
    std::string s = chunk;
    size_t nl = s.find('\n'); if (nl != std::string::npos) s = s.substr(0, nl);
    auto startsWith = [](const std::string& a, const char* b){ return a.rfind(b, 0) == 0; };
    auto contains = [](const std::string& a, const char* b){ return a.find(b) != std::string::npos; };
    auto collapseSpace = [](std::string x){ std::string y; bool sp=false; for(char c: x){ if (std::isspace((unsigned char)c)) { if (!sp) { y.push_back(' '); sp=true; } } else { y.push_back(c); sp=false; } } if (!y.empty() && y.back()==' ') y.pop_back(); return y; };
    s = collapseSpace(s);

    // band/bor/bxor: insert 'and'
    if ((startsWith(s, "band ") || startsWith(s, "bor ") || startsWith(s, "bxor ")) && !contains(s, " and ")) {
        size_t firstSpace = s.find(' ');
        if (firstSpace != std::string::npos) {
            size_t secondSpace = s.find(' ', firstSpace + 1);
            if (secondSpace != std::string::npos) {
                std::string fixed = s.substr(0, secondSpace) + " and" + s.substr(secondSpace);
                out.suggestion = "Insert 'and' between arguments";
                out.fixed = fixed; return true;
            }
        }
    }
    // shl/shr: insert 'by'
    if ((startsWith(s, "shl ") || startsWith(s, "shr ")) && !contains(s, " by ")) {
        size_t firstSpace = s.find(' ');
        if (firstSpace != std::string::npos) {
            size_t secondSpace = s.find(' ', firstSpace + 1);
            if (secondSpace != std::string::npos) {
                std::string fixed = s.substr(0, secondSpace) + " by" + s.substr(secondSpace);
                out.suggestion = "Insert 'by' before the shift amount";
                out.fixed = fixed; return true;
            }
        }
    }
    // call NAME args -> call NAME with args (replace commas with ' and ')
    if (startsWith(s, "call ") && !contains(s, " with ")) {
        size_t fnStart = 5; size_t fnEnd = s.find(' ', fnStart);
        if (fnEnd != std::string::npos && fnEnd + 1 < s.size()) {
            std::string args = s.substr(fnEnd + 1);
            for (size_t i = 0; i < args.size(); ++i) if (args[i] == ',') args.replace(i, 1, " and ");
            std::string fixed = s.substr(0, fnEnd) + " with " + args;
            out.suggestion = "Use 'with' and 'and' to separate arguments";
            out.fixed = fixed; return true;
        }
    }
    // set ... to ... (insert 'to' before last term)
    if (startsWith(s, "set ") && !contains(s, " to ")) {
        size_t pos = s.rfind(' ');
        if (pos != std::string::npos && pos > 4) {
            std::string fixed = s.substr(0, pos) + " to" + s.substr(pos);
            out.suggestion = "Insert 'to' before the value";
            out.fixed = fixed; return true;
        }
    }
    // replace S X with Y (insert 'with' before last term)
    if (startsWith(s, "replace ") && !contains(s, " with ")) {
        size_t pos = s.rfind(' ');
        if (pos != std::string::npos && pos > 8) {
            std::string fixed = s.substr(0, pos) + " with" + s.substr(pos);
            out.suggestion = "Insert 'with' before the replacement";
            out.fixed = fixed; return true;
        }
    }
    return false;
}

static void printCaretForLexeme(const std::string& preview, const std::string& nearLexeme) {
    if (nearLexeme.empty()) return;
    size_t pos = preview.find(nearLexeme);
    if (pos == std::string::npos) return;
    std::cerr << "  " << preview << "\n  ";
    for (size_t i = 0; i < pos; ++i) std::cerr << (preview[i] == '\t' ? '\t' : ' ');
    std::cerr << "^" << std::endl;
}

void runFile(const std::string& path, bool /*useVM*/, const std::string& outBytecodePath = "") {
    // Compile English source to bytecode then run natively
    std::ifstream file(path);
    if (!file.is_open()) { std::cerr << "Could not open file: " << path << std::endl; return; }
    std::stringstream buffer; buffer << file.rdbuf(); std::string source = buffer.str();
    // Expand simple imports/includes: use "path" or import "path"
    try {
        std::unordered_set<std::string> visiting; std::filesystem::path base = std::filesystem::path(path).parent_path();
        source = expandImports(source, base, visiting);
    } catch (...) {}
    // Build origin map for error reporting across includes
    std::vector<LineOrigin> origins = buildLineOrigins(source, path);
    std::vector<std::unique_ptr<Stmt>> program;
    std::vector<std::uint32_t> stmtLines; std::vector<std::uint32_t> stmtCols;
    int appliedFixes = 0;
    std::istringstream iss(source); std::string line; std::uint32_t currentLine = 0;
    while (std::getline(iss, line)) {
        currentLine++;
        auto ltrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin()); };
        auto rtrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); };
        if (line.empty()) continue;
        size_t firstNs = 0; while (firstNs < line.size() && std::isspace((unsigned char)line[firstNs])) firstNs++;
        std::string trimmed = line; ltrim(trimmed); rtrim(trimmed);
        if (trimmed.empty()) continue; 
        if (trimmed[0] == ';' || trimmed[0] == '#') continue;
        // Skip C-style comments
        if (trimmed.substr(0, 2) == "//") continue;
        
        // Handle block comments that may span multiple lines
        if (trimmed.substr(0, 2) == "/*") {
            // Skip lines until we find the closing */
            bool foundEnd = trimmed.find("*/") != std::string::npos;
            while (!foundEnd && std::getline(iss, line)) {
                currentLine++;
                std::string t = line; ltrim(t); rtrim(t);
                if (t.find("*/") != std::string::npos) {
                    foundEnd = true;
                    break;
                }
            }
            continue;
        }
        
        std::string chunk = trimmed;
        auto count_sub = [](const std::string& s, const std::string& sub){ size_t pos=0; int c=0; while ((pos = s.find(sub, pos)) != std::string::npos) { ++c; pos += sub.size(); } return c; };
        auto count_char = [](const std::string& s, char c){ int cnt=0; for(char ch : s) if(ch==c) cnt++; return cnt; };
        
        // Count both English block markers and concise block markers
        int opens = countWordOutsideQuotesAndBalanced(trimmed, "do") + count_char(trimmed, '{');
        int closes = countWordOutsideQuotesAndBalanced(trimmed, "end") + count_char(trimmed, '}');
        
        std::uint32_t stmtStartLine = currentLine; std::uint32_t stmtStartCol = static_cast<std::uint32_t>(firstNs + 1);
        while (opens > closes) {
            std::string next;
            if (!std::getline(iss, next)) break;
            currentLine++;
            std::string t = next; ltrim(t); rtrim(t);
            if (t.empty() || t[0]==';' || t[0]=='#' || t.substr(0,2)=="//") { continue; }
            // Skip block comments (and import sentinels) entirely
            if (t.substr(0, 2) == "/*") {
                bool foundEnd = t.find("*/") != std::string::npos;
                while (!foundEnd && std::getline(iss, next)) {
                    currentLine++;
                    std::string t2 = next; ltrim(t2); rtrim(t2);
                    if (t2.find("*/") != std::string::npos) { foundEnd = true; break; }
                }
                continue;
            }
            chunk += "\n" + t;
            opens += countWordOutsideQuotesAndBalanced(t, "do") + count_char(t, '{');
            closes += countWordOutsideQuotesAndBalanced(t, "end") + count_char(t, '}');
        }
        try { Lexer elex(chunk, static_cast<int>(stmtStartLine)); auto etoks = elex.scanTokens(); Parser eparser(etoks); auto stmt = eparser.parseCommand(); program.push_back(std::move(stmt)); stmtLines.push_back(stmtStartLine); stmtCols.push_back(stmtStartCol); }
        catch (const std::exception& e) {
            std::cerr << "Parse Error at line " << stmtStartLine << ": " << e.what();
            if (stmtStartLine > 0 && stmtStartLine <= origins.size()) {
                const auto& o = origins[stmtStartLine - 1];
                std::cerr << "\n  at " << o.filePath << ":" << o.localLine;
            }
            std::cerr << std::endl;
            Fix fx; bool autoFix = envFlagDefaultOn("CB_AUTOFIX");
            if (suggestFix(chunk, fx) && autoFix) {
                try { Lexer elex2(fx.fixed, static_cast<int>(stmtStartLine)); auto toks2 = elex2.scanTokens(); Parser eparser2(toks2); auto stmt2 = eparser2.parseCommand(); program.push_back(std::move(stmt2)); stmtLines.push_back(stmtStartLine); stmtCols.push_back(stmtStartCol); appliedFixes++; }
                catch (...) {}
            }
        }
    }
    Codegen cg; cg.chunk.sourceName = path; cg.setStmtLines(stmtLines); cg.stmtCols = stmtCols; cg.emit(program);
    if (!outBytecodePath.empty()) { std::ofstream ofs(outBytecodePath, std::ios::binary); if (ofs.is_open()) { writeChunk(cg.chunk, ofs); ofs.flush(); if (!ofs.good()) { std::cerr << "Failed writing bytecode: " << outBytecodePath << std::endl; } else { std::cout << "Wrote bytecode: " << outBytecodePath << std::endl; } } }
    (void)runChunk(cg.chunk);
}


// Interpreter mode removed (use native runtime + JIT)


void runBytecodeFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) { std::cerr << "Could not open bytecode: " << path << std::endl; return; }
    try { Chunk c = loadChunk(ifs); (void)runChunk(c); } catch (const std::exception& e) { std::cerr << "Error loading bytecode: " << e.what() << std::endl; }
}


void runEnglishRepl() {
    std::cout << "CatBeater Console (native). Type commands. 'exit' to quit." << std::endl;
    for (;;) {
        std::cout << "> ";
        std::string line;
        if (!std::getline(std::cin, line)) break;

        // trim
        size_t i = 0, j = line.size();
        while (i < j && std::isspace(static_cast<unsigned char>(line[i]))) i++;
        while (j > i && std::isspace(static_cast<unsigned char>(line[j-1]))) j--;
        if (i == j) continue;
        std::string trimmed = line.substr(i, j - i);
        if (trimmed == "exit" || trimmed == "quit") break;
        if (trimmed[0] == ';' || trimmed[0] == '#') continue;

        try {
            Lexer elex(trimmed, 1);
            auto etoks = elex.scanTokens();
            Parser eparser(etoks);
            auto stmt = eparser.parseCommand();
            std::vector<std::unique_ptr<Stmt>> prog; prog.push_back(std::move(stmt));
            Codegen cg; cg.chunk.sourceName = "<repl>"; cg.setStmtLines({1}); cg.stmtCols = {1}; cg.emit(prog);
            (void)runChunk(cg.chunk);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
}

// Compile English source to a bytecode chunk (no VM run)
static int compileToChunk(const std::string& path, Chunk& outChunk, const std::string& outBytecodePath = "") {
    std::ifstream file(path);
    if (!file.is_open()) { std::cerr << "Could not open file: " << path << std::endl; return 1; }
    std::stringstream buffer; buffer << file.rdbuf(); std::string source = buffer.str();
    // Expand simple imports/includes
    try {
        std::unordered_set<std::string> visiting; std::filesystem::path base = std::filesystem::path(path).parent_path();
        source = expandImports(source, base, visiting);
    } catch (...) {}
    // Build origin map for error reporting across includes
    std::vector<LineOrigin> origins = buildLineOrigins(source, path);
    std::vector<std::unique_ptr<Stmt>> program;
    std::vector<std::uint32_t> stmtLines;
    std::vector<std::uint32_t> stmtCols;
    int appliedFixes = 0;
    std::istringstream iss(source);
    std::string line;
    std::uint32_t currentLine = 0;
    while (std::getline(iss, line)) {
        currentLine++;
        auto ltrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin()); };
        auto rtrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); };
        if (line.empty()) continue;
        size_t firstNs = 0; while (firstNs < line.size() && std::isspace((unsigned char)line[firstNs])) firstNs++;
        std::string trimmed = line; ltrim(trimmed); rtrim(trimmed);
        if (trimmed.empty()) continue;
        if (trimmed[0] == ';' || trimmed[0] == '#') continue;
        // Skip C-style comments
        if (trimmed.substr(0, 2) == "//") continue;
        
        // Handle block comments that may span multiple lines
        if (trimmed.substr(0, 2) == "/*") {
            // Skip lines until we find the closing */
            bool foundEnd = trimmed.find("*/") != std::string::npos;
            while (!foundEnd && std::getline(iss, line)) {
                currentLine++;
                std::string t = line; ltrim(t); rtrim(t);
                if (t.find("*/") != std::string::npos) {
                    foundEnd = true;
                    break;
                }
            }
            continue;
        }
        
        std::string chunk = trimmed;
        auto count_sub = [](const std::string& s, const std::string& sub){ size_t pos=0; int c=0; while ((pos = s.find(sub, pos)) != std::string::npos) { ++c; pos += sub.size(); } return c; };
        auto count_char = [](const std::string& s, char c){ int cnt=0; for(char ch : s) if(ch==c) cnt++; return cnt; };
        
        // Count both English block markers and concise block markers
        int opens = countWordOutsideQuotesAndBalanced(trimmed, "do") + count_char(trimmed, '{');
        int closes = countWordOutsideQuotesAndBalanced(trimmed, "end") + count_char(trimmed, '}');
        std::uint32_t stmtStartLine = currentLine;
        std::uint32_t stmtStartCol = static_cast<std::uint32_t>(firstNs + 1);
        while (opens > closes) {
            std::string next;
            if (!std::getline(iss, next)) break;
            currentLine++;
            std::string t = next; ltrim(t); rtrim(t);
            if (t.empty() || t[0]==';' || t[0]=='#' || t.substr(0,2)=="//") { continue; }
            // Skip block comments (and import sentinels) entirely
            if (t.substr(0, 2) == "/*") {
                bool foundEnd = t.find("*/") != std::string::npos;
                while (!foundEnd && std::getline(iss, next)) {
                    currentLine++;
                    std::string t2 = next; ltrim(t2); rtrim(t2);
                    if (t2.find("*/") != std::string::npos) { foundEnd = true; break; }
                }
                continue;
            }
            chunk += "\n" + t;
            opens += countWordOutsideQuotesAndBalanced(t, "do") + count_char(t, '{');
            closes += countWordOutsideQuotesAndBalanced(t, "end") + count_char(t, '}');
        }
        try {
            Lexer elex(chunk, static_cast<int>(stmtStartLine));
            auto etoks = elex.scanTokens();
            Parser eparser(etoks);
            auto stmt = eparser.parseCommand();
            program.push_back(std::move(stmt));
            stmtLines.push_back(stmtStartLine);
            stmtCols.push_back(stmtStartCol);
        } catch (const std::exception& e) {
            std::cerr << "Parse Error at line " << stmtStartLine << ": " << e.what();
            if (stmtStartLine > 0 && stmtStartLine <= origins.size()) {
                const auto& o = origins[stmtStartLine - 1];
                std::cerr << "\n  at " << o.filePath << ":" << o.localLine;
            }
            std::cerr << std::endl;
            Fix fx; bool autoFix = envFlagDefaultOn("CB_AUTOFIX");
            if (suggestFix(chunk, fx)) {
                std::cerr << "Suggestion: " << fx.suggestion << "\nTry: " << fx.fixed << std::endl;
                if (autoFix) {
                    std::cerr << "Auto-fix applied at line " << stmtStartLine << ": '" << chunk << "' -> '" << fx.fixed << "'" << std::endl;
                    try { Lexer elex2(fx.fixed, static_cast<int>(stmtStartLine)); auto toks2 = elex2.scanTokens(); Parser eparser2(toks2); auto stmt2 = eparser2.parseCommand(); program.push_back(std::move(stmt2)); stmtLines.push_back(stmtStartLine); stmtCols.push_back(stmtStartCol); appliedFixes++; continue; } catch (...) {}
                }
            }
        }
    }
    Codegen cg; cg.chunk.sourceName = path; cg.setStmtLines(stmtLines); cg.stmtCols = stmtCols; cg.emit(program);
    if (!outBytecodePath.empty()) { std::ofstream ofs(outBytecodePath, std::ios::binary); if (ofs.is_open()) { writeChunk(cg.chunk, ofs); ofs.flush(); if (!ofs.good()) { std::cerr << "Failed writing bytecode: " << outBytecodePath << std::endl; } else { std::cout << "Wrote bytecode: " << outBytecodePath << std::endl; } } }
    outChunk = cg.chunk;
    if (appliedFixes > 0) { std::cerr << "[info] Applied " << appliedFixes << " auto-fix" << (appliedFixes==1?"":"es") << ". Set CB_AUTOFIX=0 to disable." << std::endl; }
    return 0;
}

int main(int argc, char* argv[]) {
    FFIConfig::initDefaultSearchDirs();
    {
        auto addFromEnv = [&](const char* name){
            char* envVal = nullptr; size_t envLen = 0;
#if defined(_MSC_VER)
            if (_dupenv_s(&envVal, &envLen, name) == 0 && envVal && envLen > 0) {
                std::string v(envVal);
                std::string cur;
                for (char c : v) {
                    if (c == ';' || c == ':') { if (!cur.empty()) { FFIConfig::addSearchDir(cur); cur.clear(); } }
                    else { cur.push_back(c); }
                }
                if (!cur.empty()) FFIConfig::addSearchDir(cur);
            }
            if (envVal) { free(envVal); envVal = nullptr; }
#else
            const char* v = std::getenv(name);
            if (v && v[0]) {
                std::string s(v);
                std::string cur;
                for (char c : s) {
                    if (c == ';' || c == ':') { if (!cur.empty()) { FFIConfig::addSearchDir(cur); cur.clear(); } }
                    else { cur.push_back(c); }
                }
                if (!cur.empty()) FFIConfig::addSearchDir(cur);
            }
#endif
        };
        addFromEnv("CB_DLL_PATH");
    }
    // If invoked without args, try run embedded payload from the exe itself
    if (argc <= 1) {
        std::string selfPath = makeAbsolute(argv[0] ? argv[0] : "");
        std::string payload;
        if (!selfPath.empty() && tryLoadEmbeddedFromModule(selfPath, payload)) {
            std::istringstream iss(std::string(payload.data(), payload.size()));
            try { Chunk c = loadChunk(iss); return runChunk(c); } catch (const std::exception& e) { std::cerr << "Embedded run error: " << e.what() << std::endl; return 1; }
        }
    }

    bool useVM = false;
    bool useNative = true;
    bool useInterp = false; // deprecated
    bool useJIT = false;
    std::string script;
    bool runBytecode = false;
    std::string outBytecode;
    // Bundling options
    bool bundleExe = false;
    std::string outExePath;
    std::string catInputPath; // optional: bundle an existing .cat
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vm") { useVM = true; continue; }
        // --interp removed; keep parsing for compatibility but ignore
        if (arg == "--interp" || arg == "--interpreter") { continue; }
        if (arg == "--jit") { useJIT = true; continue; }
        if (arg == "--native") { useNative = true; useVM = false; continue; }
        if (arg == "--emit" && i + 1 < argc) { outBytecode = argv[++i]; continue; }
        if (arg == "--run" && i + 1 < argc) { runBytecode = true; script = argv[++i]; continue; }
        if (arg == "--bundle-exe" && i + 2 < argc) { bundleExe = true; script = argv[++i]; outExePath = argv[++i]; continue; }
        if (arg == "--cat" && i + 1 < argc) { catInputPath = argv[++i]; continue; }
        script = arg;
    }

    // Bundling flow
    if (bundleExe) {
        Chunk c;
        int ec = 0;
        if (!catInputPath.empty()) {
            std::ifstream ifs(catInputPath, std::ios::binary);
            if (!ifs.is_open()) { std::cerr << "bundle: could not open .cat: " << catInputPath << std::endl; return 1; }
            try { c = loadChunk(ifs); } catch (const std::exception& e) { std::cerr << "bundle: invalid .cat: " << e.what() << std::endl; return 1; }
        } else {
            if (script.empty()) { std::cerr << "bundle: missing source path (.cb or .cat)." << std::endl; return 1; }
            if (script.size() >= 4 && script.substr(script.size()-4) == ".cat") {
                std::ifstream ifs(script, std::ios::binary);
                if (!ifs.is_open()) { std::cerr << "bundle: could not open .cat: " << script << std::endl; return 1; }
                try { c = loadChunk(ifs); } catch (const std::exception& e) { std::cerr << "bundle: invalid .cat: " << e.what() << std::endl; return 1; }
            } else {
                ec = compileToChunk(script, c, "");
                if (ec != 0) return ec;
            }
        }
        std::string selfPath = makeAbsolute(argv[0] ? argv[0] : "");
        if (selfPath.empty()) { std::cerr << "bundle: could not determine stub exe path." << std::endl; return 1; }
        if (!writeBundledFile(selfPath, outExePath, c)) return 1;
        std::cout << "Wrote bundled exe: " << outExePath << std::endl;
        return 0;
    }

    if (runBytecode) {
        std::ifstream ifs(script, std::ios::binary);
        if (!ifs.is_open()) { std::cerr << "Could not open bytecode: " << script << std::endl; return 1; }
        try { Chunk c = loadChunk(ifs); return runChunk(c); } catch (const std::exception& e) { std::cerr << "Error loading bytecode: " << e.what() << std::endl; return 1; }
    }

    if (!script.empty()) {
        // If user passed a .cat file without --run, load and run natively
        if (script.size() >= 4 && script.substr(script.size()-4) == ".cat") {
            std::ifstream ifs(script, std::ios::binary);
            if (!ifs.is_open()) { std::cerr << "Could not open bytecode: " << script << std::endl; return 1; }
            try { Chunk c = loadChunk(ifs); return runChunk(c); } catch (const std::exception& e) { std::cerr << "Error loading bytecode: " << e.what() << std::endl; return 1; }
        }
        if (useNative && useJIT) {
            // Native JIT evaluation of last numeric expression
            std::ifstream file(script);
            if (!file.is_open()) { std::cerr << "Could not open file: " << script << std::endl; return 1; }
            std::stringstream buffer; buffer << file.rdbuf(); std::string source = buffer.str();
            // Parse complete program in English mode, then JIT only the last expression statement
            std::vector<std::unique_ptr<Stmt>> program;
            std::istringstream iss(source);
            std::string line;
            std::uint32_t lineNo = 0;
            while (std::getline(iss, line)) {
                lineNo++;
                std::string trimmed = line;
                auto ltrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin()); };
                auto rtrim = [](std::string& s){ while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); };
                ltrim(trimmed); rtrim(trimmed);
                if (trimmed.empty() || trimmed[0]==';' || trimmed[0]=='#') continue;
                try {
                    Lexer elex(trimmed, static_cast<int>(lineNo));
                    auto toks = elex.scanTokens();
                    Parser parser(toks);
                    auto stmt = parser.parseCommand();
                    program.push_back(std::move(stmt));
                } catch (const std::exception& e) {
                    std::cerr << "Parse Error at line " << lineNo << ": " << e.what() << std::endl;
                }
            }
            const ExpressionStmt* lastExprStmt = nullptr;
            for (auto it = program.rbegin(); it != program.rend(); ++it) {
                if (auto* es = dynamic_cast<ExpressionStmt*>(it->get())) { lastExprStmt = es; break; }
            }
            if (!lastExprStmt) {
                std::cerr << "No final numeric expression to JIT." << std::endl;
                return 1;
            }
            X86_64JIT jit;
            if (!jit.canCompile(lastExprStmt->expression.get())) {
                std::cerr << "Expression not supported by native JIT." << std::endl;
                return 1;
            }
            double result = jit.executeExpression(lastExprStmt->expression.get());
            double tr = std::trunc(result);
            if (std::fabs(result - tr) < 1e-12) { std::cout << static_cast<long long>(tr) << std::endl; }
            else { std::cout << result << std::endl; }
        } else if (useNative) {
            // Default: compile to .cat next to the source (or to --emit path)
            std::filesystem::path sp(script);
            std::filesystem::path outPath = sp;
            if (!outBytecode.empty()) outPath = outBytecode; else outPath.replace_extension(".cat");
            Chunk c;
            int ec = compileToChunk(script, c, outPath.string());
            if (ec != 0) return ec;
            // no run by default
        } else {
            runFile(script, useVM, outBytecode);
        }
        return 0;
    }

    std::cout << "Welcome to the CatBeater compiler (native)." << std::endl;
    // Run test.cat by default, then execute any selfhosted bytecode if present
    {
        // Compile the self-host compiler to bytecode and run it, so it can emit demo.cat
        Chunk c; int ec = compileToChunk("test_modern.cat", c, "");
        if (ec == 0) { (void)runChunk(c); }
        // If a self-hosted bytecode was produced (selfhost/demo.cat), run it
        std::ifstream shifs("selfhost/demo.cat", std::ios::binary);
        if (shifs.is_open()) {
            try { Chunk sc = loadChunk(shifs); (void)runChunk(sc); } catch (...) {}
        } else {
            std::cerr << "[selfhost] no demo.cat produced.\n";
        }
    }
    std::cout << "\n";
    runEnglishRepl();
    return 0;
}