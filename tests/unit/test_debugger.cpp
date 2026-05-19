#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "debugger/DebuggerRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

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
