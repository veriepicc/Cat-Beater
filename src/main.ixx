import <iostream>;
import <fstream>;
import <sstream>;
import <string_view>;
import <cctype>;
import <vector>;
import <string>;
import <memory>;
import <cstdlib>;
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
        int opens = count_sub(trimmed, " do") + count_char(trimmed, '{');
        int closes = ((trimmed == "end") ? 1 : count_sub(trimmed, " end")) + count_char(trimmed, '}');
        
        std::uint32_t stmtStartLine = currentLine; std::uint32_t stmtStartCol = static_cast<std::uint32_t>(firstNs + 1);
        while (opens > closes) { 
            std::string next; 
            if (!std::getline(iss, next)) break; 
            std::string t = next; ltrim(t); rtrim(t); 
            if (t.empty() || t[0]==';' || t[0]=='#' || t.substr(0,2)=="//") continue; 
            chunk += "\n" + t; 
            opens += count_sub(t, " do") + count_char(t, '{'); 
            closes += ((t == "end") ? 1 : count_sub(t, " end")) + count_char(t, '}'); 
            currentLine++; 
        }
        try { Lexer elex(chunk, static_cast<int>(stmtStartLine)); auto etoks = elex.scanTokens(); Parser eparser(etoks); auto stmt = eparser.parseCommand(); program.push_back(std::move(stmt)); stmtLines.push_back(stmtStartLine); stmtCols.push_back(stmtStartCol); }
        catch (const std::exception& e) {
            std::cerr << "Parse Error at line " << stmtStartLine << ": " << e.what() << std::endl;
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
        int opens = count_sub(trimmed, " do") + count_char(trimmed, '{');
        int closes = ((trimmed == "end") ? 1 : count_sub(trimmed, " end")) + count_char(trimmed, '}');
        std::uint32_t stmtStartLine = currentLine;
        std::uint32_t stmtStartCol = static_cast<std::uint32_t>(firstNs + 1);
        while (opens > closes) {
            std::string next;
            if (!std::getline(iss, next)) break;
            std::string t = next; ltrim(t); rtrim(t);
            if (t.empty() || t[0]==';' || t[0]=='#' || t.substr(0,2)=="//") continue;
            chunk += "\n" + t;
            opens += count_sub(t, " do") + count_char(t, '{');
            closes += ((t == "end") ? 1 : count_sub(t, " end")) + count_char(t, '}');
            currentLine++;
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
            std::cerr << "Parse Error at line " << stmtStartLine << ": " << e.what() << std::endl;
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
    bool useVM = false;
    bool useNative = true;
    std::string script;
    bool runBytecode = false;
    std::string outBytecode;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vm") { useVM = true; continue; }
        if (arg == "--native") { useNative = true; useVM = false; continue; }
        if (arg == "--emit" && i + 1 < argc) { outBytecode = argv[++i]; continue; }
        if (arg == "--run" && i + 1 < argc) { runBytecode = true; script = argv[++i]; continue; }
        script = arg;
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
        if (useNative) {
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
            // Find last ExpressionStmt and try to JIT it if it's a pure numeric expression
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
            // Print like interpreter: integers without decimal when whole
            double tr = std::trunc(result);
            if (std::fabs(result - tr) < 1e-12) { std::cout << static_cast<long long>(tr) << std::endl; }
            else { std::cout << result << std::endl; }
        } else {
            runFile(script, useVM, outBytecode);
        }
        return 0;
    }

    std::cout << "Welcome to the CatBeater compiler (native)." << std::endl;
    // Always: compile test.cat English to bytecode, then run bytecode natively.
    {
        Chunk c; int ec = compileToChunk("test.cat", c, "");
        if (ec == 0) { (void)runChunk(c); }
    }
    std::cout << "\n";
    runEnglishRepl();
    return 0;
}
