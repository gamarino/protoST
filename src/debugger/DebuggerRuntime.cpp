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

void DebuggerRuntime::enterSession(STRuntime& rt, DebugFrame frame, const std::string& reason) {
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

        if (line == "c" || line == "cont")    { setCommand(Command::Continue); return; }
        if (line == "s" || line == "step")    { setCommand(Command::Step);     return; }
        if (line == "n" || line == "next")    { setCommand(Command::Next);     return; }
        if (line == "f" || line == "finish")  { setCommand(Command::Finish);   return; }
        if (line == "q" || line == "quit")    { std::exit(0); }

        if (line == "where" || line == "bt") {
            out << "frame depth: " << frame.frameDepth << "\n";
            out << "pc: "          << frame.pc << " / " << frame.module->bytes().size() << "\n";
            // dump next 6 instructions
            for (size_t k = 0; k < 6 && (frame.pc + k * 2) < frame.module->bytes().size(); ++k) {
                Op op = static_cast<Op>(frame.module->bytes()[frame.pc + k*2]);
                uint8_t arg = frame.module->bytes()[frame.pc + k*2 + 1];
                out << "  " << (frame.pc + k*2) << ": op=" << static_cast<int>(op) << " arg=" << static_cast<int>(arg) << "\n";
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
            std::string src = line.substr(line.find(' ') + 1) + ".";
            protoST::Parser pp(src);
            auto ast = pp.parseModule();
            if (!pp.errors().empty()) {
                for (auto& e : pp.errors()) out << "  parse error: " << e.message << "\n";
                continue;
            }
            protoST::Compiler cc;
            auto bc = cc.compileModule(*ast);
            if (cc.hasErrors()) {
                for (auto& s : cc.errors()) out << "  compile error: " << s << "\n";
                continue;
            }
            try {
                auto* r = rt.runTopLevel(*bc);
                if (r == PROTO_NONE)       out << "  nil\n";
                else if (r == PROTO_TRUE)  out << "  true\n";
                else if (r == PROTO_FALSE) out << "  false\n";
                else {
                    try { out << "  " << r->asLong(rt.rootCtx()) << "\n"; }
                    catch (...) {
                        auto* s = r->asString(rt.rootCtx());
                        out << "  " << (s ? s->toStdString(rt.rootCtx()) : std::string("<obj>")) << "\n";
                    }
                }
            } catch (const std::exception& e) {
                out << "  error: " << e.what() << "\n";
            }
            continue;
        }

        out << "?? unknown command: " << line << " (try: c, s, n, f, q, where, locals, print)\n";
    }
}

} // namespace protoST
