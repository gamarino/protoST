#include "DebuggerRuntime.h"
#include "protoST/STRuntime.h"
#include "../runtime/BytecodeModule.h"
#include "../runtime/Opcodes.h"
#include "../frontend/Parser.h"
#include "../frontend/Compiler.h"
#include "protoCore.h"
#include <iostream>
#include <sstream>
#include <cctype>

namespace protoST {

namespace {
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}
} // anon

bool DebuggerRuntime::evaluateExpression(STRuntime& rt, const std::string& expr,
                                         std::string& out) {
    // Append the statement terminator protoST expects; the caller passes a
    // bare expression.
    std::string src = expr + ".";
    protoST::Parser pp(src);
    auto ast = pp.parseModule();
    if (!pp.errors().empty()) {
        out = "parse error: " + pp.errors().front().message;
        return false;
    }
    protoST::Compiler cc;
    auto bc = cc.compileModule(*ast);
    if (cc.hasErrors()) {
        out = "compile error: " + cc.errors().front();
        return false;
    }
    try {
        auto* r = rt.runTopLevel(*bc);
        if (r == PROTO_NONE)       out = "nil";
        else if (r == PROTO_TRUE)  out = "true";
        else if (r == PROTO_FALSE) out = "false";
        else {
            try { out = std::to_string(r->asLong(rt.rootCtx())); }
            catch (...) {
                auto* s = r->asString(rt.rootCtx());
                out = s ? s->toStdString(rt.rootCtx()) : std::string("<obj>");
            }
        }
        return true;
    } catch (const std::exception& e) {
        out = std::string("error: ") + e.what();
        return false;
    }
}

void DebuggerRuntime::enterSession(STRuntime& rt, DebugFrame frame, const std::string& reason) {
    // F8-3: when a frontend is installed (e.g. the DAP adapter), route the
    // stop through it instead of the built-in text REPL. The frontend blocks
    // until the user resumes and returns the resume command; we apply the
    // command/mode it chose and return. The `-d` text path (frontend_ ==
    // nullptr) below is unchanged.
    if (frontend_) {
        Command cmd = frontend_->onStopped(rt, frame, reason);
        setCommand(cmd);
        switch (cmd) {
            case Command::Continue: setMode(Mode::Free);        break;
            case Command::Step:     setMode(Mode::SingleStep);  break;
            case Command::Next:     setMode(Mode::SingleStep);  break;
            case Command::Finish:   setMode(Mode::RunToReturn); break;
        }
        return;
    }

    auto& out = outStream_ ? *outStream_ : std::cout;
    auto& in  = inStream_  ? *inStream_  : std::cin;

    out << "halted: " << reason << "\n";
    out << "  pc=" << frame.pc << " stack=" << frame.stack.size()
        << " locals=" << frame.locals.size() << "\n";

    while (true) {
        out << "(stdbg) " << std::flush;
        std::string line;
        if (!std::getline(in, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        if (line == "c" || line == "cont")    { setMode(Mode::Free);        setCommand(Command::Continue); return; }
        // F2: `next` is aliased to `step` because true frame-depth tracking
        // for `next` requires the F3 call stack (not yet implemented).
        if (line == "s" || line == "step")    { setMode(Mode::SingleStep);  setCommand(Command::Step);     return; }
        if (line == "n" || line == "next")    { setMode(Mode::SingleStep);  setCommand(Command::Next);     return; }
        // F2: `finish` falls back to Free (same as cont) because method
        // call frames aren't yet implemented. True RunToReturn requires F3.
        if (line == "f" || line == "finish")  { setMode(Mode::RunToReturn); setCommand(Command::Finish);   return; }
        if (line == "q" || line == "quit")    { std::exit(0); }

        if (line == "where" || line == "bt") {
            out << "frame depth: " << frame.frameDepth << "\n";
            out << "pc: "          << frame.pc << " / " << frame.module->bytes().size() << "\n";
            // Dump the next 6 instructions. BL-2: instructions are variable
            // width (an EXTEND prefix word per extra operand byte), so step
            // by the real decoded width rather than a fixed 2 bytes.
            const auto& dbg = frame.module->bytes();
            size_t cur = frame.pc;
            for (size_t k = 0; k < 6 && cur + 1 < dbg.size(); ++k) {
                size_t start = cur;
                Op op = static_cast<Op>(dbg[cur]);
                unsigned int arg = dbg[cur + 1];
                cur += 2;
                while (op == Op::EXTEND && cur + 1 < dbg.size()) {
                    op  = static_cast<Op>(dbg[cur]);
                    arg = (arg << 8) | dbg[cur + 1];
                    cur += 2;
                }
                out << "  " << start << ": op=" << static_cast<int>(op)
                    << " arg=" << static_cast<int>(arg) << "\n";
            }
            continue;
        }
        if (line == "locals") {
            for (size_t k = 0; k < frame.locals.size(); ++k) {
                out << "  [" << k << "] " << (frame.locals[k] ? "<obj>" : "nil") << "\n";
            }
            continue;
        }
        if (line.rfind("print ", 0) == 0 || line.rfind("p ", 0) == 0) {
            // F8-4: parse -> compile -> run -> format now lives in the shared
            // evaluateExpression helper, reused by the DAP `evaluate` request.
            std::string result;
            evaluateExpression(rt, line.substr(line.find(' ') + 1), result);
            out << "  " << result << "\n";
            continue;
        }

        if (line.rfind("break ", 0) == 0) {
            try {
                size_t bpc = std::stoul(line.substr(6));
                breakpoints_.add(frame.module, bpc);
                out << "  break set at pc=" << bpc << "\n";
            } catch (...) { out << "  invalid pc\n"; }
            continue;
        }
        if (line == "info breaks") {
            out << "  " << breakpoints_.size() << " breakpoints set\n";
            continue;
        }
        if (line.rfind("clear ", 0) == 0) {
            try {
                size_t bpc = std::stoul(line.substr(6));
                breakpoints_.remove(frame.module, bpc);
                out << "  cleared pc=" << bpc << "\n";
            } catch (...) { out << "  invalid pc\n"; }
            continue;
        }

        out << "?? unknown command: " << line << " (try: c, s, n, f, q, where, locals, print, break, clear, info breaks)\n";
    }
}

} // namespace protoST
