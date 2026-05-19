#include "protoST/STRuntime.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options] <script.st> [args...]\n"
        "       %s -e '<expr>'\n"
        "       %s -i                     (REPL — F7)\n"
        "       %s -d <script.st>         (debugger — F2)\n"
        "       %s --dump-ast <script.st>\n"
        "       %s venv <subcommand> [args]\n"
        "       %s compile <script.st> -o <out.stbc>\n"
        "\nOptions:\n"
        "  -e '<expr>'    Evaluate expression and print result\n"
        "  -i             Start REPL (F7)\n"
        "  -d             Debug script (F2)\n"
        "  --dump-ast     Parse and dump AST (F1)\n"
        "  --help         Show this message\n"
        "  --version      Show version\n",
        prog, prog, prog, prog, prog, prog, prog);
}

void printVersion() {
    std::printf("%s\n", protoST::versionString());
}

} // anon

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(argv[0]); return 64; }
    std::string mode = argv[1];

    if (mode == "--help" || mode == "-h")   { printUsage(argv[0]); return 0; }
    if (mode == "--version" || mode == "-v"){ printVersion();      return 0; }
    if (mode == "--dump-ast")               { return 1; /* implemented in Task 23 */ }
    if (mode == "-e")                       { return 1; /* implemented in Task 48 */ }
    if (mode == "-d")                       { return 1; /* implemented in Task 56 */ }
    if (mode == "venv")                     { return 1; /* implemented in Task 24 */ }

    // unknown leading flag → treat as script path (future): for now error.
    std::fprintf(stderr, "Unknown option or mode: %s\n", argv[1]);
    return 64;
}
