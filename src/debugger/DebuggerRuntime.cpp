#include "DebuggerRuntime.h"
#include "protoST/STRuntime.h"
#include "../runtime/BytecodeModule.h"
#include "../runtime/Opcodes.h"
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
    (void)rt; (void)frame;
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
        out << "?? unknown command: " << line << " (try: c, s, n, f, q)\n";
    }
}

} // namespace protoST
