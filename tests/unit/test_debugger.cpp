#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "debugger/DebuggerRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

#include <sstream>

TEST_CASE("Debugger: halt on Object is a no-op when detached", "[debugger]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    REQUIRE(!rt.debugger().attached());
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r == PROTO_NONE);
}

TEST_CASE("Debugger: halt when attached enters the session (stub) and returns nil", "[debugger]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    // The session reads its own stream (S18): with std::cin it waited for a
    // terminal whenever ctest ran without stdin redirected.
    std::istringstream in("cont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r == PROTO_NONE);
}

#include <sstream>

TEST_CASE("Debugger: session reads a 'cont' command and resumes", "[debugger][session]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    std::istringstream in("cont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r == PROTO_NONE);
    REQUIRE(out.str().find("halted") != std::string::npos);
}

TEST_CASE("Debugger: where shows pc and instructions; print evaluates", "[debugger][cmds]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    std::istringstream in("where\nprint 1 + 2\ncont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    rt.runTopLevel(*bc);
    auto text = out.str();
    REQUIRE(text.find("pc:") != std::string::npos);
    REQUIRE(text.find("  3")  != std::string::npos);   // result of 1+2
}

TEST_CASE("Debugger: single-step halts every instruction until cont", "[debugger][step]") {
    protoST::Parser P("nil halt. 1 + 2.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    // Step a couple of times then continue
    std::istringstream in("step\nstep\nstep\ncont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 3);
    // Should have entered the session at least 3 times (the initial halt + 2 steps)
    size_t count = 0; size_t pos = 0;
    while ((pos = out.str().find("(stdbg)", pos)) != std::string::npos) { ++count; ++pos; }
    REQUIRE(count >= 3);
}

TEST_CASE("Debugger: location breakpoint halts at given pc", "[debugger][bp]") {
    protoST::Parser P("1 + 2.");          // PUSH_CONST 0; PUSH_CONST 1; SEND_BINARY 2; RETURN_TOP
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    rt.debugger().breakpoints().add(bc.get(), 4);  // before SEND_BINARY

    std::istringstream in("where\ncont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 3);
    REQUIRE(out.str().find("breakpoint") != std::string::npos);
    REQUIRE(out.str().find("pc: 4")     != std::string::npos);
}

TEST_CASE("Debugger: single-step through inlined and:/or:, both guard paths", "[debugger][step]") {
    // `true and: [...]` takes the Boolean path of JUMP_IF_NOT_BOOL; `3 or:
    // [...]` its out-of-line doesNotUnderstand:, resumed with 10, which is
    // the value of the expression (the block does not run).
    protoST::Parser P("nil halt. (true and: [ 1 + 2 ]) + "
                      "([ 3 or: [ 100 ] ] on: MessageNotUnderstood do: [ :e | e resume: 10 ]).");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    protoST::STRuntime rt;
    rt.debugger().attach();
    std::string cmds;
    for (int i = 0; i < 60; ++i) cmds += "step\n";
    cmds += "cont\n";
    std::istringstream in(cmds);
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 13);
    size_t count = 0; size_t pos = 0;
    while ((pos = out.str().find("(stdbg)", pos)) != std::string::npos) { ++count; ++pos; }
    REQUIRE(count >= 10);
}

TEST_CASE("Debugger: an inlined block's temporary is shown by its name inside the block",
          "[debugger][vars]") {
    // `[ | t | ... ]` of an inlined ifTrue: lives in a hidden slot of the
    // enclosing frame (0.10.0); the variables view names it `t` over the
    // block's code only.
    protoST::Parser P("| n | n := 0. true ifTrue: [ | t | t := 5. n := t ]. n.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    protoST::STRuntime rt;
    const protoST::BytecodeModule::ScopedLocal* t = nullptr;
    for (const auto& sl : bc->scopedLocals())
        if (sl.name == "t") t = &sl;
    REQUIRE(t != nullptr);
    protoST::DebugFrame f;
    f.module = bc.get();
    f.locals.assign(bc->localNames().size(), PROTO_NONE);
    REQUIRE(t->slot < f.locals.size());
    f.locals[t->slot] = proto::makeSmallInt(5);
    auto valueOf = [&](size_t pc, const std::string& name) -> const proto::ProtoObject* {
        f.pc = pc;
        for (const auto& v : protoST::DebuggerRuntime::frameVariables(rt, rt.rootCtx(), f))
            if (v.name == name) return v.value;
        return nullptr;
    };
    REQUIRE(valueOf(t->fromPc, "t") == proto::makeSmallInt(5));
    REQUIRE(valueOf(t->toPc, "t") == nullptr);
}
