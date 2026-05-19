#include "Venv.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace protoST {

namespace {

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

fs::path templateDir() {
    // Resolved relative to the binary at runtime via PROTOST_TEMPLATE_DIR
    // (set by CMakeLists.txt as a compile-time macro). The tests rely on
    // this default; production installs override via env STENV_TEMPLATE_DIR.
    if (const char* env = std::getenv("STENV_TEMPLATE_DIR")) return fs::path(env);
    return fs::path(PROTOST_TEMPLATE_DIR);
}

} // anon

int venvCreate(const std::string& venvPath,
               const std::string& homeBin,
               const std::string& version) {
    fs::path venv(venvPath);
    if (fs::exists(venv)) {
        std::fprintf(stderr, "venv path already exists: %s\n", venvPath.c_str());
        return 1;
    }
    std::error_code ec;
    fs::create_directories(venv / "bin", ec);
    fs::create_directories(venv / "lib" / "protoST" / "modules", ec);
    fs::create_directories(venv / "cache" / "bytecode", ec);
    if (ec) {
        std::fprintf(stderr, "venv mkdir failed: %s\n", ec.message().c_str());
        return 2;
    }

    // stenv.cfg
    {
        auto tpl = readAll(templateDir() / "stenv.cfg.in");
        tpl = replaceAll(tpl, "@HOME_BIN@", homeBin);
        tpl = replaceAll(tpl, "@VERSION@",  version);
        std::ofstream f(venv / "stenv.cfg");
        f << tpl;
    }
    // activate
    {
        auto tpl = readAll(templateDir() / "activate");
        tpl = replaceAll(tpl, "@VENV_PATH@", fs::absolute(venv).string());
        std::ofstream f(venv / "bin" / "activate");
        f << tpl;
    }
    return 0;
}

std::string venvDiscover(const std::string& cwd) {
    if (const char* e = std::getenv("STENV"); e && *e) return std::string(e);
    fs::path p = cwd.empty() ? fs::current_path() : fs::path(cwd);
    for (; !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / ".venv" / "stenv.cfg")) return (p / ".venv").string();
        if (p == p.root_path()) break;
    }
    return "";
}

int venvInfo(const std::string& cwd) {
    auto v = venvDiscover(cwd);
    if (v.empty()) { std::puts("no active venv"); return 1; }
    std::printf("venv: %s\n", v.c_str());
    std::ifstream f(fs::path(v) / "stenv.cfg");
    std::string line;
    while (std::getline(f, line)) std::printf("  %s\n", line.c_str());
    return 0;
}

int venvActivateSnippet(const std::string& venvPath) {
    auto p = fs::path(venvPath) / "bin" / "activate";
    if (!fs::exists(p)) { std::fprintf(stderr, "not a venv: %s\n", venvPath.c_str()); return 1; }
    std::printf(". %s\n", p.string().c_str());
    return 0;
}

} // namespace protoST
