#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

TEST_CASE("ExecutionEngine: empty module returns nil", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.emit(protoST::Op::PUSH_NIL, 0);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* result = rt.runTopLevel(m);
    REQUIRE(result == PROTO_NONE);   // nil maps to PROTO_NONE
}

TEST_CASE("ExecutionEngine: PUSH_TRUE returns true sentinel", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.emit(protoST::Op::PUSH_TRUE, 0);
    m.emit(protoST::Op::RETURN_TOP, 0);

    REQUIRE(rt.runTopLevel(m) == PROTO_TRUE);
}

TEST_CASE("ExecutionEngine: PUSH_FALSE returns false sentinel", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.emit(protoST::Op::PUSH_FALSE, 0);
    m.emit(protoST::Op::RETURN_TOP, 0);

    REQUIRE(rt.runTopLevel(m) == PROTO_FALSE);
}

TEST_CASE("ExecutionEngine: empty bytestream returns nil", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    REQUIRE(rt.runTopLevel(m) == PROTO_NONE);
}

TEST_CASE("Engine: PUSH_CONST returns the materialised integer", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(42);
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    auto* ctx = rt.rootCtx();
    REQUIRE(r->asLong(ctx) == 42);   // protoCore's ProtoObject::asLong
}

TEST_CASE("Engine: locals round-trip via STORE_LOCAL / PUSH_LOCAL", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(7);
    m.emit(protoST::Op::PUSH_CONST,  0);   // 7
    m.emit(protoST::Op::DUP,         0);
    m.emit(protoST::Op::STORE_LOCAL, 0);   // x := 7 (leaves 7 on stack)
    m.emit(protoST::Op::POP,         0);
    m.emit(protoST::Op::PUSH_LOCAL,  0);   // x
    m.emit(protoST::Op::RETURN_TOP,  0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("Engine: SEND raises on unknown selector", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(1);
    m.internSymbol("noSuch");
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::SEND_UNARY, 1);
    m.emit(protoST::Op::RETURN_TOP, 0);

    REQUIRE_THROWS_WITH(rt.runTopLevel(m), Catch::Matchers::ContainsSubstring("doesNotUnderstand"));
}

TEST_CASE("Engine: 1 + 2 returns 3", "[engine][primitives]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(1); m.addInteger(2); m.internSymbol("+");
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::PUSH_CONST, 1);
    m.emit(protoST::Op::SEND_BINARY, 2);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r->asLong(rt.rootCtx()) == 3);   // protoCore uses asLong, not toLong
}

TEST_CASE("Engine: 'ab' , 'cd' returns 'abcd'", "[engine][primitives]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addString("ab"); m.addString("cd"); m.internSymbol(",");
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::PUSH_CONST, 1);
    m.emit(protoST::Op::SEND_BINARY, 2);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    // protoCore exposes UTF-8 via asString(ctx)->toStdString(ctx).
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "abcd");
}

TEST_CASE("Engine: [ :a :b | a + b ] value: 3 value: 4 returns 7", "[engine][block]") {
    protoST::Parser P("[ :a :b | a + b ] value: 3 value: 4.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("Engine: true ifTrue: [ 42 ] returns 42", "[engine][block]") {
    protoST::Parser P("true ifTrue: [ 42 ].");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("Engine: captured locals roundtrip at top-level", "[engine][closures]") {
    // `i` is referenced inside the inner block, so the closure analysis marks
    // it as captured in the outer (top-level) scope. The compiler then emits
    // STORE_CAPTURED for `i := 7` and PUSH_CAPTURED for the trailing `i`.
    // We never invoke the block — we only need the top-level frame to
    // successfully round-trip the value through the captured dict.
    protoST::Parser P("i := 7. [ i ]. i.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("Engine: F2 hero — closed-form sum 1..100 returns 5050", "[engine][hero]") {
    const char* src = "[ :n | n * (n + 1) / 2 ] value: 100.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 5050);
}

TEST_CASE("Engine: block captures outer mutable state via shared dict", "[engine][closures]") {
    // Top-level declares i := 0. The inner block writes i := 42.
    // After the block is invoked (via `value`), the top-level should see i == 42.
    const char* src =
        "i := 0. "
        "[ i := 42 ] value. "
        "i.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("Engine: F3 hero - sum 1..100 via whileTrue + closures = 5050", "[engine][hero][closures]") {
    const char* src =
        "sum := 0. i := 1. "
        "[ i <= 100 ] whileTrue: [ sum := sum + i. i := i + 1 ]. "
        "sum.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 5050);
}

TEST_CASE("Engine: STRuntime exposes globals with Object pre-registered", "[engine][globals]") {
    protoST::STRuntime rt;
    auto* g = rt.globals();
    REQUIRE(g != nullptr);
    auto* ctx = rt.rootCtx();
    auto* sym = ctx->fromUTF8String("Object")->asString(ctx);
    auto* obj = g->getAttribute(ctx, sym);
    REQUIRE(obj != nullptr);
    REQUIRE(obj != PROTO_NONE);
}

TEST_CASE("Engine: Object>>newChild returns a fresh mutable instance", "[engine][globals]") {
    // Hand-build bytecode: PUSH_GLOBAL #Object; SEND_UNARY #newChild; RETURN_TOP
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.internSymbol("Object");        // const idx 0
    m.internSymbol("newChild");      // const idx 1
    m.emit(protoST::Op::PUSH_GLOBAL, 0);
    m.emit(protoST::Op::SEND_UNARY, 1);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r != nullptr);
    REQUIRE(r != PROTO_NONE);
}

TEST_CASE("Engine: STORE_GLOBAL + PUSH_GLOBAL roundtrip", "[engine][globals]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(42);             // idx 0
    m.internSymbol("Foo");        // idx 1
    m.emit(protoST::Op::PUSH_CONST,   0);   // push 42
    m.emit(protoST::Op::DUP,          0);   // keep one copy on stack
    m.emit(protoST::Op::STORE_GLOBAL, 1);   // Foo := 42  (consumes one copy)
    m.emit(protoST::Op::POP,          0);   // drop the residual
    m.emit(protoST::Op::PUSH_GLOBAL,  1);   // load Foo
    m.emit(protoST::Op::RETURN_TOP,   0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

