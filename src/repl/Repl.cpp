#include "repl/Repl.h"

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

#include <readline/readline.h>
#include <readline/history.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

namespace protoST {

namespace {

// --- small helpers ----------------------------------------------------------

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string historyPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return std::string();
    return std::string(home) + "/.protost_history";
}

// Lexical balance scan. Returns true if every '(' / '[' is closed and string
// delimiters ('...') and comment delimiters ("...") are balanced. Smalltalk
// strings use single quotes (with '' as an embedded quote); comments use
// double quotes. We treat them as plain toggles which is sufficient for the
// "definitely incomplete" pre-check — an embedded '' simply toggles twice and
// nets out balanced.
bool lexicallyBalanced(const std::string& src) {
    int paren = 0, bracket = 0;
    bool inStr = false, inComment = false;
    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (inStr) {
            if (c == '\'') inStr = false;
            continue;
        }
        if (inComment) {
            if (c == '"') inComment = false;
            continue;
        }
        switch (c) {
            case '\'': inStr = true; break;
            case '"':  inComment = true; break;
            case '(':  ++paren; break;
            case ')':  --paren; break;
            case '[':  ++bracket; break;
            case ']':  --bracket; break;
            default: break;
        }
    }
    return !inStr && !inComment && paren <= 0 && bracket <= 0
        && paren == 0 && bracket == 0;
}

bool endsWithPeriod(const std::string& src) {
    // Inspect the last non-whitespace, non-comment character.
    std::string t = trim(src);
    if (t.empty()) return false;
    return t.back() == '.';
}

// Read one logical line. Uses readline for an interactive tty (history,
// arrows, Ctrl-R); falls back to std::getline for piped / non-tty stdin so
// tests are deterministic. `*eof` is set true on end-of-input.
bool readLine(const char* prompt, bool interactive, std::string& out, bool* eof) {
    *eof = false;
    if (interactive) {
        char* line = ::readline(prompt);
        if (!line) { *eof = true; return false; }
        out.assign(line);
        std::free(line);
        return true;
    }
    // Non-tty: echo the prompt so piped sessions are still readable, then
    // read a raw line.
    std::fputs(prompt, stdout);
    std::fflush(stdout);
    std::string buf;
    int ch;
    bool any = false;
    while ((ch = std::getc(stdin)) != EOF) {
        any = true;
        if (ch == '\n') { out = buf; return true; }
        buf.push_back(static_cast<char>(ch));
    }
    if (any) { out = buf; return true; }
    *eof = true;
    return false;
}

void printResult(STRuntime& rt, const proto::ProtoObject* r) {
    auto* ctx = rt.rootCtx();
    if (r == PROTO_NONE || r == nullptr) { std::puts("=> nil"); return; }
    if (r == PROTO_TRUE)  { std::puts("=> true");  return; }
    if (r == PROTO_FALSE) { std::puts("=> false"); return; }
    try {
        std::printf("=> %lld\n", r->asLong(ctx));
    } catch (...) {
        try {
            auto* s = r->asString(ctx);
            std::printf("=> %s\n", s ? s->toStdString(ctx).c_str() : "<obj>");
        } catch (...) {
            std::puts("=> <obj>");
        }
    }
}

void printHelp() {
    std::puts("Commands:");
    std::puts("  :help, :h    Show this message");
    std::puts("  :quit, :q    Exit the REPL (also Ctrl-D)");
    std::puts("Enter Smalltalk expressions ending with '.' to evaluate them.");
    std::puts("Incomplete input (open '[', '(', or a multi-line method) keeps");
    std::puts("reading at the '   ...> ' continuation prompt.");
}

// Completeness verdict for an accumulated buffer.
enum class Completeness { Incomplete, Complete, Error };

Completeness classify(const std::string& buffer) {
    if (!lexicallyBalanced(buffer)) return Completeness::Incomplete;

    Parser P{std::string(buffer)};
    auto ast = P.parseModule();
    if (P.errors().empty()) {
        Compiler C;
        C.setReplMode(true);
        auto bc = C.compileModule(*ast);
        (void)bc;
        if (!C.hasErrors()) return Completeness::Complete;
        // Parsed but failed to compile — that is a genuine error, not an
        // "input not finished yet" situation.
        return Completeness::Error;
    }
    // Parse errors: if the buffer does not yet end with a period the user is
    // very likely still typing — keep reading. Otherwise it is a real error.
    if (!endsWithPeriod(buffer)) return Completeness::Incomplete;
    return Completeness::Error;
}

// Parse + compile + run a complete buffer. Retains the module so installed
// method/class bytecode pointers stay valid for the rest of the session.
void evaluate(STRuntime& rt,
              std::vector<std::unique_ptr<BytecodeModule>>& retained,
              const std::string& buffer) {
    Parser P{std::string(buffer)};
    auto ast = P.parseModule();
    if (!P.errors().empty()) {
        for (auto& e : P.errors())
            std::fprintf(stderr, "<repl>:%d:%d: %s\n", e.line, e.column, e.message.c_str());
        return;
    }
    Compiler C;
    C.setReplMode(true);
    auto bc = C.compileModule(*ast);
    if (C.hasErrors()) {
        for (auto& s : C.errors())
            std::fprintf(stderr, "compile error: %s\n", s.c_str());
        return;
    }
    BytecodeModule* mod = bc.get();
    retained.push_back(std::move(bc));
    try {
        auto* r = rt.runTopLevel(*mod);
        printResult(rt, r);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "error: unknown runtime failure\n");
    }
}

} // namespace

int runRepl() {
    const bool interactive = ::isatty(STDIN_FILENO) != 0;

    std::string histPath = historyPath();
    if (interactive && !histPath.empty())
        ::read_history(histPath.c_str());

    std::puts("protoST 0.1.0-pre \xe2\x80\x94 interactive REPL");
    std::puts(":help for commands, :quit or Ctrl-D to exit");
    std::fflush(stdout);

    STRuntime rt;
    std::vector<std::unique_ptr<BytecodeModule>> retained;

    const char* primary = "protoST> ";
    const char* continuation = "   ...> ";

    std::string buffer;
    bool inMultiline = false;

    for (;;) {
        std::string line;
        bool eof = false;
        const char* prompt = inMultiline ? continuation : primary;
        if (!readLine(prompt, interactive, line, &eof)) {
            if (eof) {
                std::puts(interactive ? "\nbye" : "bye");
                break;
            }
            continue;
        }

        std::string trimmed = trim(line);

        // Meta-commands are only recognised at the primary prompt (when not
        // in the middle of accumulating a multi-line form).
        if (!inMultiline && !trimmed.empty() && trimmed[0] == ':') {
            if (trimmed == ":quit" || trimmed == ":q") {
                std::puts("bye");
                break;
            }
            if (trimmed == ":help" || trimmed == ":h") {
                printHelp();
                std::fflush(stdout);
                continue;
            }
            std::fprintf(stderr, "unknown command: %s\n", trimmed.c_str());
            continue;
        }

        if (interactive && !trimmed.empty())
            ::add_history(line.c_str());

        const bool blankLine = trimmed.empty();

        if (!inMultiline) {
            if (blankLine) continue;  // ignore blank lines at primary prompt
            buffer = line;
        } else {
            buffer += "\n";
            buffer += line;
        }

        // A blank line at the continuation prompt always forces evaluation so
        // the user can escape a stuck multi-line state.
        if (inMultiline && blankLine) {
            evaluate(rt, retained, buffer);
            buffer.clear();
            inMultiline = false;
            std::fflush(stdout);
            continue;
        }

        Completeness verdict = classify(buffer);
        if (verdict == Completeness::Incomplete) {
            inMultiline = true;
            continue;
        }
        if (verdict == Completeness::Error) {
            // Surface the real error and reset.
            evaluate(rt, retained, buffer);
            buffer.clear();
            inMultiline = false;
            std::fflush(stdout);
            continue;
        }
        // Complete.
        evaluate(rt, retained, buffer);
        buffer.clear();
        inMultiline = false;
        std::fflush(stdout);
    }

    if (interactive && !histPath.empty())
        ::write_history(histPath.c_str());

    return 0;
}

} // namespace protoST
