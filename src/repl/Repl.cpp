#include "repl/Repl.h"

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <readline/readline.h>
#include <readline/history.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <set>
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
    // BL-3: shared formatter — non-primitive objects render as "a ClassName".
    std::printf("=> %s\n", protoST::formatValue(rt, rt.rootCtx(), r).c_str());
}

void printHelp() {
    std::puts("Commands:");
    std::puts("  :help, :h         Show this message");
    std::puts("  :quit, :q         Exit the REPL (also Ctrl-D)");
    std::puts("  :load <path>      Execute a .st file in the current session");
    std::puts("  :reset            Discard all session state (vars, classes, methods)");
    std::puts("  :vars, :env       List user-defined globals in the session");
    std::puts("  :time <expr>      Evaluate <expr> and report wall-clock time");
    std::puts("  :history          Show recent input lines");
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

// The REPL session: a persistent runtime plus the bytecode modules it has
// installed. `:reset` rebuilds the whole struct from scratch.
struct Session {
    std::unique_ptr<STRuntime> rt;
    std::vector<std::unique_ptr<BytecodeModule>> retained;
    // Snapshot of the global names the runtime pre-registers at construction
    // (Object, Number, Array, …). `:vars` reports only the names absent from
    // this set, i.e. the names the user actually defined.
    std::set<std::string> builtinGlobals;

    Session() { rebuild(); }

    // Construct a fresh STRuntime and re-snapshot its builtin globals. The old
    // runtime, if any, is destroyed first — protoST's "one STRuntime per
    // process" contract (LANGUAGE.md §13.2 / D2) forbids two *simultaneously*
    // live runtimes, not a sequential rebuild.
    void rebuild() {
        retained.clear();
        rt.reset();
        rt = std::make_unique<STRuntime>();
        builtinGlobals = currentGlobalNames();
    }

    // Collect the own-attribute names of the globals namespace right now.
    std::set<std::string> currentGlobalNames() const {
        std::set<std::string> names;
        proto::ProtoObject* g = rt->globals();
        if (!g) return names;
        proto::ProtoContext* ctx = rt->rootCtx();
        const proto::ProtoSparseList* own = g->getOwnAttributes(ctx);
        if (!own) return names;
        auto* it = const_cast<proto::ProtoSparseListIterator*>(
            own->getIterator(ctx));
        while (it && it->hasNext(ctx)) {
            unsigned long key = it->nextKey(ctx);
            auto* sym = reinterpret_cast<const proto::ProtoObject*>(key)
                            ->asString(ctx);
            if (sym) names.insert(sym->toStdString(ctx));
            it = const_cast<proto::ProtoSparseListIterator*>(it->advance(ctx));
        }
        return names;
    }
};

// Parse + compile + run a complete buffer. Retains the module so installed
// method/class bytecode pointers stay valid for the rest of the session.
// `sourceName` labels diagnostics ("<repl>", or a file path for :load).
// Returns true on a clean run, false if a parse / compile / runtime error was
// reported. When `printIt` is true the result value is echoed.
bool evaluate(Session& s, const std::string& buffer,
              const char* sourceName, bool printIt) {
    Parser P{std::string(buffer)};
    auto ast = P.parseModule();
    if (!P.errors().empty()) {
        for (auto& e : P.errors())
            std::fprintf(stderr, "%s:%d:%d: %s\n",
                         sourceName, e.line, e.column, e.message.c_str());
        return false;
    }
    Compiler C;
    C.setReplMode(true);
    auto bc = C.compileModule(*ast);
    bc->setSourceName(sourceName);
    if (C.hasErrors()) {
        for (auto& str : C.errors())
            std::fprintf(stderr, "compile error: %s\n", str.c_str());
        return false;
    }
    BytecodeModule* mod = bc.get();
    s.retained.push_back(std::move(bc));
    try {
        auto* r = s.rt->runTopLevel(*mod);
        if (printIt) printResult(*s.rt, r);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return false;
    } catch (...) {
        std::fprintf(stderr, "error: unknown runtime failure\n");
        return false;
    }
}

// --- meta-command implementations -------------------------------------------

// :load <path> — read a .st file and execute it in the current session.
void cmdLoad(Session& s, const std::string& arg) {
    std::string path = trim(arg);
    if (path.empty()) {
        std::fprintf(stderr, ":load requires a file path\n");
        return;
    }
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, ":load: cannot open %s\n", path.c_str());
        return;
    }
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (n < 0) { std::fclose(fp); std::fprintf(stderr, ":load: cannot read %s\n", path.c_str()); return; }
    std::string src(static_cast<size_t>(n), '\0');
    size_t got = std::fread(src.data(), 1, static_cast<size_t>(n), fp);
    src.resize(got);
    std::fclose(fp);

    // Execute against the live session — definitions and variables persist
    // exactly as if the file's text had been typed at the prompt. The result
    // value is not echoed (a file is a sequence of statements, not one
    // expression); a clean run is confirmed instead.
    if (evaluate(s, src, path.c_str(), /*printIt=*/false))
        std::printf("loaded %s\n", path.c_str());
}

// :reset — discard all session state and start a fresh STRuntime.
void cmdReset(Session& s) {
    s.rebuild();
    std::puts("session reset — all user variables, classes and methods cleared");
}

// :vars / :env — list the user-defined globals in the current session.
void cmdVars(Session& s) {
    std::set<std::string> all = s.currentGlobalNames();
    std::vector<std::string> userNames;
    for (const auto& name : all)
        if (s.builtinGlobals.find(name) == s.builtinGlobals.end())
            userNames.push_back(name);
    if (userNames.empty()) {
        std::puts("(no user-defined globals)");
        return;
    }
    proto::ProtoObject* g = s.rt->globals();
    proto::ProtoContext* ctx = s.rt->rootCtx();
    for (const auto& name : userNames) {
        auto* key = proto::ProtoString::createSymbol(ctx, name.c_str());
        const proto::ProtoObject* val = g->getOwnAttributeDirect(ctx, key);
        std::string rendered = protoST::formatValue(*s.rt, ctx, val);
        std::printf("  %s = %s\n", name.c_str(), rendered.c_str());
    }
}

// :time <expr> — evaluate <expr>, reporting wall-clock time alongside the
// result.
void cmdTime(Session& s, const std::string& arg) {
    std::string expr = trim(arg);
    if (expr.empty()) {
        std::fprintf(stderr, ":time requires an expression\n");
        return;
    }
    auto start = std::chrono::steady_clock::now();
    bool ok = evaluate(s, expr, "<repl>", /*printIt=*/true);
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    if (ok)
        std::printf("time: %.3f ms\n", ms);
}

// :history — show the most recent input lines.
void cmdHistory() {
    HIST_ENTRY** list = ::history_list();
    if (!list || !list[0]) {
        std::puts("(history empty)");
        return;
    }
    int total = 0;
    while (list[total]) ++total;
    int from = total > 20 ? total - 20 : 0;
    for (int i = from; i < total; ++i)
        std::printf("  %d  %s\n", i + 1, list[i]->line);
}

// Dispatch a `:`-prefixed line. Returns true if the REPL should exit.
bool dispatchMeta(Session& s, const std::string& trimmed) {
    // Split into the command word and the remaining argument text.
    size_t sp = trimmed.find_first_of(" \t");
    std::string cmd = (sp == std::string::npos) ? trimmed : trimmed.substr(0, sp);
    std::string arg = (sp == std::string::npos) ? std::string()
                                                : trimmed.substr(sp + 1);

    if (cmd == ":quit" || cmd == ":q") {
        std::puts("bye");
        return true;
    }
    if (cmd == ":help" || cmd == ":h") {
        printHelp();
        std::fflush(stdout);
        return false;
    }
    if (cmd == ":load") {
        cmdLoad(s, arg);
        std::fflush(stdout);
        return false;
    }
    if (cmd == ":reset") {
        cmdReset(s);
        std::fflush(stdout);
        return false;
    }
    if (cmd == ":vars" || cmd == ":env") {
        cmdVars(s);
        std::fflush(stdout);
        return false;
    }
    if (cmd == ":time") {
        cmdTime(s, arg);
        std::fflush(stdout);
        return false;
    }
    if (cmd == ":history") {
        cmdHistory();
        std::fflush(stdout);
        return false;
    }
    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    return false;
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

    Session session;

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
            // Record the meta-command in history too so :history is honest.
            if (interactive)
                ::add_history(line.c_str());
            if (dispatchMeta(session, trimmed))
                break;
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
            evaluate(session, buffer, "<repl>", /*printIt=*/true);
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
            evaluate(session, buffer, "<repl>", /*printIt=*/true);
            buffer.clear();
            inMultiline = false;
            std::fflush(stdout);
            continue;
        }
        // Complete.
        evaluate(session, buffer, "<repl>", /*printIt=*/true);
        buffer.clear();
        inMultiline = false;
        std::fflush(stdout);
    }

    if (interactive && !histPath.empty())
        ::write_history(histPath.c_str());

    return 0;
}

} // namespace protoST
