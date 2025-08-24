export module FFI;
#if defined(_WIN32)
import <windows.h>;
#endif
import <string>;
import <unordered_map>;
import <vector>;
import <filesystem>;

// FFIConfig abstracts OS-specific loading. It exposes only void* to avoid leaking platform headers.
export namespace FFIConfig {

#if defined(_WIN32)
    inline std::unordered_map<std::string, HMODULE> gDlls;
    inline std::unordered_map<std::string, FARPROC> gSyms;
    inline std::vector<std::filesystem::path> gSearchDirs;

    export inline void addSearchDir(const std::string& dir) {
        if (dir.empty()) return;
        std::error_code ec;
        std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::path(dir), ec);
        if (ec) p = std::filesystem::path(dir);
        gSearchDirs.push_back(p);
    }

    export inline void initDefaultSearchDirs() {
        addSearchDir(std::filesystem::current_path().string());
        char buf[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::filesystem::path exePath(buf);
            std::filesystem::path exeDir = exePath.parent_path();
            addSearchDir(exeDir.string());
            addSearchDir((exeDir / "dlls").string());
        }
    }

    inline HMODULE loadModuleWithSearch(const std::string& dll) {
        if (dll.find('\\') != std::string::npos || dll.find('/') != std::string::npos) {
            HMODULE h = LoadLibraryA(dll.c_str());
            if (h) return h;
        }
        HMODULE h = LoadLibraryA(dll.c_str());
        if (!h && dll.rfind(".dll") != dll.size() - 4) {
            std::string withExt = dll + ".dll";
            h = LoadLibraryA(withExt.c_str());
            if (h) return h;
        } else if (h) {
            return h;
        }
        for (const auto& dir : gSearchDirs) {
            std::filesystem::path cand = dir / dll;
            h = LoadLibraryA(cand.string().c_str());
            if (!h) {
                std::filesystem::path cand2 = cand;
                cand2 += ".dll";
                h = LoadLibraryA(cand2.string().c_str());
            }
            if (h) return h;
        }
        return nullptr;
    }

    export inline void* loadProc(const std::string& dll, const std::string& func) {
        std::string key = dll; key.push_back('|'); key += func;
        auto it = gSyms.find(key);
        if (it != gSyms.end()) return reinterpret_cast<void*>(it->second);
        HMODULE h = nullptr;
        auto dit = gDlls.find(dll);
        if (dit == gDlls.end()) {
            h = loadModuleWithSearch(dll);
            if (!h) return nullptr;
            gDlls[dll] = h;
        } else {
            h = dit->second;
        }
        FARPROC p = GetProcAddress(h, func.c_str());
        if (!p) return nullptr;
        gSyms.emplace(std::move(key), p);
        return reinterpret_cast<void*>(p);
    }
    export inline void* loadProcOrdinal(const std::string& dll, unsigned short ordinal) {
        HMODULE h = nullptr;
        auto dit = gDlls.find(dll);
        if (dit == gDlls.end()) {
            h = loadModuleWithSearch(dll);
            if (!h) return nullptr;
            gDlls[dll] = h;
        } else {
            h = dit->second;
        }
        FARPROC p = GetProcAddress(h, MAKEINTRESOURCEA(ordinal));
        if (!p) return nullptr;
        return reinterpret_cast<void*>(p);
    }
#else
    import <dlfcn.h>;
    inline std::unordered_map<std::string, void*> gLibs;
    inline std::unordered_map<std::string, void*> gSyms;
    inline std::vector<std::filesystem::path> gSearchDirs;

    export inline void addSearchDir(const std::string& dir) {
        if (dir.empty()) return;
        std::error_code ec;
        std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::path(dir), ec);
        if (ec) p = std::filesystem::path(dir);
        gSearchDirs.push_back(p);
    }
    export inline void initDefaultSearchDirs() {
        addSearchDir(std::filesystem::current_path().string());
        std::error_code ec; auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            std::filesystem::path exeDir = exe.parent_path();
            addSearchDir(exeDir.string());
            addSearchDir((exeDir / "lib").string());
        }
    }
    inline void* loadModuleWithSearch(const std::string& so) {
        void* h = dlopen(so.c_str(), RTLD_NOW);
        if (!h) {
            std::string withSo = so;
            if (so.find(".so") == std::string::npos && so.find(".dylib") == std::string::npos) {
                withSo = so + ".so";
            }
            h = dlopen(withSo.c_str(), RTLD_NOW);
        }
        if (h) return h;
        for (const auto& dir : gSearchDirs) {
            std::filesystem::path cand = dir / so;
            h = dlopen(cand.string().c_str(), RTLD_NOW);
            if (!h) {
                std::filesystem::path cand2 = cand; cand2 += ".so";
                h = dlopen(cand2.string().c_str(), RTLD_NOW);
            }
            if (h) return h;
        }
        return nullptr;
    }
    export inline void* loadProc(const std::string& lib, const std::string& func) {
        std::string key = lib; key.push_back('|'); key += func;
        auto it = gSyms.find(key); if (it != gSyms.end()) return it->second;
        void* h = nullptr;
        auto dit = gLibs.find(lib);
        if (dit == gLibs.end()) {
            h = loadModuleWithSearch(lib);
            if (!h) return nullptr;
            gLibs[lib] = h;
        } else {
            h = dit->second;
        }
        void* p = dlsym(h, func.c_str());
        if (!p) return nullptr;
        gSyms.emplace(std::move(key), p);
        return p;
    }
    export inline void* loadProcOrdinal(const std::string&, unsigned short) { return nullptr; }
#endif
}

