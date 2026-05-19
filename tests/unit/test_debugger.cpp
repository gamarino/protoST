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
