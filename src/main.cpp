#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/ASTPrinter.h"
#include "frontend/Compiler.h"
#include "runtime/Venv.h"
#include "debugger/DebuggerRuntime.h"
#include "repl/Repl.h"
#include "protoCore.h"
#include <cstdio>
#include <cstring>
#include <exception>
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
    if (mode == "--dump-ast") {
        if (argc < 3) { std::fprintf(stderr, "--dump-ast requires a path\n"); return 64; }
        const char* path = argv[2];
        std::FILE* fp = std::fopen(path, "rb");
        if (!fp) { std::fprintf(stderr, "cannot open %s\n", path); return 66; }
        std::fseek(fp, 0, SEEK_END); long n = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
        std::string src(static_cast<size_t>(n), '\0');
        std::fread(src.data(), 1, static_cast<size_t>(n), fp);
        std::fclose(fp);

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
    if (mode == "-e") {
        if (argc < 3) { std::fprintf(stderr, "-e requires an expression\n"); return 64; }
        std::string src = argv[2];
        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        for (auto& e : P.errors())
            std::fprintf(stderr, "<expr>:%d:%d: %s\n", e.line, e.column, e.message.c_str());
        if (!P.errors().empty()) return 65;

        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        if (C.hasErrors()) {
            for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
            return 70;
        }
        try {
            protoST::STRuntime rt;
            auto* r = rt.runTopLevel(*bc);
            auto* ctx = rt.rootCtx();
            if (r == PROTO_NONE)            std::puts("nil");
            else if (r == PROTO_TRUE)       std::puts("true");
            else if (r == PROTO_FALSE)      std::puts("false");
            else {
                try { std::printf("%lld\n", r->asLong(ctx)); }
                catch (...) {
                    auto s = r->asString(ctx) ? r->asString(ctx)->toStdString(ctx) : std::string("<obj>");
                    std::puts(s.c_str());
                }
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    if (mode == "-d") {
        if (argc < 3) { std::fprintf(stderr, "-d requires a path\n"); return 64; }
        const char* path = argv[2];
        std::FILE* fp = std::fopen(path, "rb");
        if (!fp) { std::fprintf(stderr, "cannot open %s\n", path); return 66; }
        std::fseek(fp, 0, SEEK_END); long n = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
        std::string src(static_cast<size_t>(n), '\0');
        std::fread(src.data(), 1, static_cast<size_t>(n), fp); std::fclose(fp);

        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        if (!P.errors().empty()) {
            for (auto& e : P.errors())
                std::fprintf(stderr, "%s:%d:%d: %s\n", path, e.line, e.column, e.message.c_str());
            return 65;
        }
        protoST::Compiler C; auto bc = C.compileModule(*ast);
        if (C.hasErrors()) {
            for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
            return 70;
        }
        protoST::STRuntime rt;
        rt.debugger().attach();
        try {
            auto* r = rt.runTopLevel(*bc);
            auto* ctx = rt.rootCtx();
            if (r && r != PROTO_NONE) {
                try { std::printf("=> %lld\n", r->asLong(ctx)); }
                catch (...) { std::printf("=> <obj>\n"); }
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    if (mode == "venv") {
        if (argc < 3) { std::fprintf(stderr, "venv requires a subcommand: create|activate|info\n"); return 64; }
        std::string sub = argv[2];
        if (sub == "create") {
            std::string path = (argc >= 4) ? argv[3] : ".venv";
            return protoST::venvCreate(path, "/usr/local/bin", "0.1.0");
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

    // Default: treat argv[1] as a path to a .st script file and execute it.
    {
        const char* path = argv[1];
        std::FILE* fp = std::fopen(path, "rb");
        if (!fp) { std::fprintf(stderr, "Unknown option or mode: %s\n", path); return 64; }
        std::fseek(fp, 0, SEEK_END); long n = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
        std::string src(static_cast<size_t>(n), '\0');
        std::fread(src.data(), 1, static_cast<size_t>(n), fp);
        std::fclose(fp);

        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        for (auto& e : P.errors())
            std::fprintf(stderr, "%s:%d:%d: %s\n", path, e.line, e.column, e.message.c_str());
        if (!P.errors().empty()) return 65;

        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        if (C.hasErrors()) {
            for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
            return 70;
        }
        try {
            protoST::STRuntime rt;
            auto* r = rt.runTopLevel(*bc);
            auto* ctx = rt.rootCtx();
            if (r == PROTO_NONE)            std::puts("nil");
            else if (r == PROTO_TRUE)       std::puts("true");
            else if (r == PROTO_FALSE)      std::puts("false");
            else {
                try { std::printf("%lld\n", r->asLong(ctx)); }
                catch (...) {
                    auto s = r->asString(ctx) ? r->asString(ctx)->toStdString(ctx) : std::string("<obj>");
                    std::puts(s.c_str());
                }
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
}
