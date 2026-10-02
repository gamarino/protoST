#include "Venv.h"
#include "VenvTemplates.h"
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

// A template: from STENV_TEMPLATE_DIR when that is set (to try out edited
// templates without rebuilding), else the copy compiled into protost
// (VenvTemplates.h, generated from src/venv_template), so an installed or
// unpacked protost needs no template directory.
std::string templateText(const std::string& name) {
    if (const char* env = std::getenv("STENV_TEMPLATE_DIR"); env && *env)
        return readAll(fs::path(env) / name);
    for (const auto& f : venv_templates::kFiles)
        if (name == f.name) return f.text;
    return std::string();
}

// The activation scripts written into <venv>/bin, one per shell, on every
// platform (a Windows user may use Git Bash, and pwsh runs everywhere).
// cmd.exe reads its batch files best with CR LF line ends.
struct Script { const char* name; bool crlf; };
constexpr Script kScripts[] = {
    {"activate", false},        // sh, bash, zsh
    {"activate.fish", false},   // fish
    {"Activate.ps1", false},    // PowerShell
    {"activate.bat", true},     // cmd.exe
    {"deactivate.bat", true},
};

std::string withCrlf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (char c : text) {
        if (c == '\n' && (out.empty() || out.back() != '\r')) out.push_back('\r');
        out.push_back(c);
    }
    return out;
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
        auto tpl = templateText("stenv.cfg.in");
        tpl = replaceAll(tpl, "@HOME_BIN@", homeBin);
        tpl = replaceAll(tpl, "@VERSION@",  version);
        std::ofstream f(venv / "stenv.cfg", std::ios::binary);
        f << tpl;
    }
    // The activation scripts name the venv by its absolute path, in the
    // platform's own form (backslashes on Windows).
    const std::string venvAbs = fs::absolute(venv).make_preferred().string();
    for (const Script& sc : kScripts) {
        std::string text = templateText(sc.name);
        if (text.empty()) {
            std::fprintf(stderr, "venv template missing: %s\n", sc.name);
            return 2;
        }
        text = replaceAll(text, "@VENV_PATH@", venvAbs);
        if (sc.crlf) text = withCrlf(text);
        std::ofstream f(venv / "bin" / sc.name, std::ios::binary);
        f << text;
        if (!f) {
            std::fprintf(stderr, "venv: cannot write %s\n", (venv / "bin" / sc.name).string().c_str());
            return 2;
        }
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
    // The command that activates the venv in the platform's usual shell:
    // cmd.exe on Windows (PowerShell users run bin\Activate.ps1), a POSIX
    // shell elsewhere.
#if defined(_WIN32)
    auto p = fs::path(venvPath) / "bin" / "activate.bat";
    if (!fs::exists(p)) { std::fprintf(stderr, "not a venv: %s\n", venvPath.c_str()); return 1; }
    std::printf("call \"%s\"\n", p.make_preferred().string().c_str());
#else
    auto p = fs::path(venvPath) / "bin" / "activate";
    if (!fs::exists(p)) { std::fprintf(stderr, "not a venv: %s\n", venvPath.c_str()); return 1; }
    std::printf(". %s\n", p.string().c_str());
#endif
    return 0;
}

} // namespace protoST
