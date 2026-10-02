#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/ASTPrinter.h"
#include "frontend/Compiler.h"
#include "runtime/Venv.h"
#include "debugger/DebuggerRuntime.h"
#include "repl/Repl.h"
#include "dap/DapServer.h"
#include "runtime/ValueFormat.h"
#include "runtime/UnhandledSTException.h"
#include "protoCore.h"
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

// Windows: the standard streams carry exactly the bytes the program writes, as
// on Linux and macOS (no "\n" -> "\r\n" translation), and a console shows and
// reads them as UTF-8. The process code page is UTF-8 through the manifest
// (src/windows/utf8.manifest), so argv, getenv and paths are UTF-8 too.
void prepareStandardStreams() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void printUsage(const char* prog) {
    // D13: `compile <script.st> -o <out.stbc>` was advertised here but never
    // implemented (bytecode serialisation is out of scope). The usage text is
    // kept honest — only the modes the binary actually supports are listed.
    std::fprintf(stderr,
        "Usage: %s [--print-last] <script.st> [args...]   (\"-\" reads the script from stdin)\n"
        "       %s -e '<expr>'\n"
        "       %s -i                     (interactive REPL)\n"
        "       %s -d <script.st>         (CLI debugger)\n"
        "       %s --dap                  (Debug Adapter Protocol server)\n"
        "       %s --dump-ast <script.st>\n"
        "       %s venv <subcommand> [args]\n"
        "\nOptions:\n"
        "  --print-last   After the script, print the value of its last statement\n"
        "  -e '<expr>'    Evaluate expression and print result\n"
        "  -i             Start the interactive REPL\n"
        "  -d             Run the script under the CLI debugger\n"
        "  --dap          Run the Debug Adapter Protocol server over stdin/stdout\n"
        "  --dump-ast     Parse the script and print its AST\n"
        "  --help         Show this message\n"
        "  --version      Show version\n",
        prog, prog, prog, prog, prog, prog, prog);
}

// Reads a whole script from `path`; "-" names standard input. Uses stream
// reads rather than fseek/ftell so a pipe, /dev/stdin or a process
// substitution works: those are not seekable, and the size ftell reported for
// them was garbage.
bool readWholeFile(const char* path, std::string& out) {
    if (std::strcmp(path, "-") == 0) {
        // Binary on every platform (prepareStandardStreams), so a script read
        // from a pipe means the same bytes as the file it came from.
        out.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        return !std::cin.bad();
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void printVersion() {
    std::printf("%s\n", protoST::versionString());
}

// PROTOST_REPORT_PEAK_RSS=1: when main returns, report the process's peak
// resident memory on stderr as "protost: peak resident set size <N> KB". The
// same figure on every platform, measured by the process itself:
// getrusage's ru_maxrss (what GNU and BSD time print; kilobytes on Linux,
// bytes on macOS) and the peak working set on Windows, which has no time(1).
// tests/cli/test_cli_memory_bounded.sh bounds it.
struct PeakMemoryReport {
    const bool enabled = [] {
        const char* v = std::getenv("PROTOST_REPORT_PEAK_RSS");
        return v && v[0] == '1';
    }();
    ~PeakMemoryReport() {
        if (!enabled) return;
        unsigned long long kb = 0;
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof pmc;
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return;
        kb = static_cast<unsigned long long>(pmc.PeakWorkingSetSize) / 1024;
#else
        rusage ru{};
        if (getrusage(RUSAGE_SELF, &ru) != 0) return;
#if defined(__APPLE__)
        kb = static_cast<unsigned long long>(ru.ru_maxrss) / 1024;
#else
        kb = static_cast<unsigned long long>(ru.ru_maxrss);
#endif
#endif
        std::fprintf(stderr, "protost: peak resident set size %llu KB\n", kb);
    }
};

} // anon

int main(int argc, char** argv) {
    prepareStandardStreams();
    const PeakMemoryReport peakMemoryReport;
#if defined(__linux__) && defined(PR_SET_PTRACER)
    // Diagnostics: PROTOST_ALLOW_PTRACE=1 lets a debugger that is not this
    // process's ancestor attach (gdb -p) under kernel.yama.ptrace_scope=1,
    // so a hung run can be inspected without re-running it under gdb.
    if (const char* v = std::getenv("PROTOST_ALLOW_PTRACE"); v && v[0] == '1')
        prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif
    if (argc < 2) { printUsage(argv[0]); return 64; }
    std::string mode = argv[1];

    if (mode == "--help" || mode == "-h")   { printUsage(argv[0]); return 0; }
    if (mode == "--version" || mode == "-v"){ printVersion();      return 0; }
    if (mode == "--dump-ast") {
        if (argc < 3) { std::fprintf(stderr, "--dump-ast requires a path\n"); return 64; }
        const char* path = argv[2];
        std::string src;
        if (!readWholeFile(path, src)) { std::fprintf(stderr, "file not found: %s\n", path); return 66; }

        protoST::Parser P(std::move(src));
        auto m = P.parseModule();
        for (auto& e : P.errors())
            std::fprintf(stderr, "%s:%d:%d: %s\n", path, e.line, e.column, e.message.c_str());
        std::fputs(protoST::astToString(*m).c_str(), stdout);
        return P.errors().empty() ? 0 : 65;
    }
    if (mode == "-i") {
        return protoST::runRepl();
    }
    if (mode == "--dap") {
        return protoST::runDapServer();
    }
    if (mode == "-e") {
        if (argc < 3) { std::fprintf(stderr, "-e requires an expression\n"); return 64; }
        std::string src = argv[2];
        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        for (auto& e : P.errors())
            std::fprintf(stderr, "<expr>:%d:%d: %s\n", e.line, e.column, e.message.c_str());
        if (!P.errors().empty()) return 65;

        try {
            std::unique_ptr<protoST::BytecodeModule> bc;  // outlives rt (see above)
            protoST::STRuntime rt;
            protoST::Compiler C;
            bc = C.compileModule(*ast);
            bc->setSourceName("<expr>");
            if (C.hasErrors()) {
                for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
                return 70;
            }
            auto* r = rt.runTopLevel(*bc);
            // BL-3: shared formatter — non-primitive objects render as
            // "a ClassName" via the default printString logic.
            std::puts(protoST::formatValue(rt, rt.rootCtx(), r).c_str());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", protoST::describeUncaught(e).c_str());
            return 1;
        }
    }
    if (mode == "-d") {
        if (argc < 3) { std::fprintf(stderr, "-d requires a path\n"); return 64; }
        const char* path = argv[2];
        protoST::setProgramArguments(path, std::vector<std::string>(argv + 3, argv + argc));
        std::string src;
        if (!readWholeFile(path, src)) { std::fprintf(stderr, "file not found: %s\n", path); return 66; }

        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        if (!P.errors().empty()) {
            for (auto& e : P.errors())
                std::fprintf(stderr, "%s:%d:%d: %s\n", path, e.line, e.column, e.message.c_str());
            return 65;
        }
        std::unique_ptr<protoST::BytecodeModule> bc;  // outlives rt (see above)
        protoST::STRuntime rt;
        protoST::Compiler C; bc = C.compileModule(*ast);
        bc->setSourceName(path);
        if (C.hasErrors()) {
            for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
            return 70;
        }
        rt.debugger().attach();
        try {
            auto* r = rt.runTopLevel(*bc);
            if (r && r != PROTO_NONE) {
                // BL-3: shared formatter.
                std::printf("=> %s\n",
                    protoST::formatValue(rt, rt.rootCtx(), r).c_str());
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", protoST::describeUncaught(e).c_str());
            return 1;
        }
    }
    if (mode == "venv") {
        if (argc < 3) { std::fprintf(stderr, "venv requires a subcommand: create|activate|info\n"); return 64; }
        std::string sub = argv[2];
        if (sub == "create") {
            std::string path = (argc >= 4) ? argv[3] : ".venv";
            return protoST::venvCreate(path, "/usr/local/bin", PROTOST_VERSION);
        }
        if (sub == "activate") {
            std::string p = (argc >= 4) ? argv[3] : protoST::venvDiscover("");
            if (p.empty()) { std::fprintf(stderr, "no venv to activate\n"); return 1; }
            return protoST::venvActivateSnippet(p);
        }
        if (sub == "info") return protoST::venvInfo("");
        std::fprintf(stderr, "unknown venv subcommand: %s\n", sub.c_str());
        return 64;
    }

    // Default: a .st script file. It prints what the program prints and
    // nothing else; `--print-last` also prints the value of its last
    // statement (the conformance runner checks that value). Echoing it by
    // default (D12b) showed `x` twice for a script ending in `x printNl.`.
    {
        bool printLast = false;
        int argi = 1;
        if (mode == "--print-last") {
            if (argc < 3) { std::fprintf(stderr, "--print-last requires a path\n"); return 64; }
            printLast = true;
            argi = 2;
        }
        const char* path = argv[argi];
        if (path[0] == '-' && path[1] != '\0') { std::fprintf(stderr, "unknown option: %s\n", path); printUsage(argv[0]); return 64; }
        protoST::setProgramArguments(path, std::vector<std::string>(argv + argi + 1, argv + argc));
        std::string src;
        if (!readWholeFile(path, src)) { std::fprintf(stderr, "file not found: %s\n", path); return 66; }

        // Diagnostics name a script read from standard input "<stdin>".
        const char* sourceName = std::strcmp(path, "-") == 0 ? "<stdin>" : path;
        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        for (auto& e : P.errors())
            std::fprintf(stderr, "%s:%d:%d: %s\n", sourceName, e.line, e.column, e.message.c_str());
        if (!P.errors().empty()) return 65;

        try {
            // The runtime (and with it the kernel) exists before the script is
            // compiled, so the compiler knows the kernel classes' instance
            // variables (a subclass of IOStream reads `fd`).
            // `bc` is declared before `rt` so it is destroyed after it: the
            // runtime's workers may still run this module's methods while it
            // shuts down.
            std::unique_ptr<protoST::BytecodeModule> bc;
            protoST::STRuntime rt;
            protoST::Compiler C;
            bc = C.compileModule(*ast);
            bc->setSourceName(sourceName);
            if (C.hasErrors()) {
                for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
                return 70;
            }
            auto* r = rt.runTopLevel(*bc);
            // BL-3: shared formatter.
            if (printLast) std::puts(protoST::formatValue(rt, rt.rootCtx(), r).c_str());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", protoST::describeUncaught(e).c_str());
            return 1;
        }
    }
}
