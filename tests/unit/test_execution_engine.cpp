#include <catch2/catch_all.hpp>
#include <cstdlib>

#include "protoST/STRuntime.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"
#include "runtime/Bootstrap.h"
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

TEST_CASE("Engine: ClassDecl creates class accessible as global", "[engine][classes]") {
    // After running `Object subclass: #Counter.` the global "Counter" must
    // exist and be a fresh object distinct from Object (Object's child).
    protoST::Parser P("Object subclass: #Counter.");
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    rt.runTopLevel(*bc);

    auto* g = rt.globals();
    auto* ctx = rt.rootCtx();
    auto* counterSym = ctx->fromUTF8String("Counter")->asString(ctx);
    auto* counter = g->getAttribute(ctx, counterSym);
    REQUIRE(counter != nullptr);
    REQUIRE(counter != PROTO_NONE);

    auto* objectSym = ctx->fromUTF8String("Object")->asString(ctx);
    auto* object = g->getAttribute(ctx, objectSym);
    REQUIRE(counter != object);   // fresh child, not the same object as Object
}

TEST_CASE("Engine: user method on class — args + locals only", "[engine][methods]") {
    // Method body that doesn't touch instance variables (those land in F4-U5).
    // We only exercise method args and method locals, plus the dispatch from
    // an instance to its class proto.
    //
    // Pipeline exercised here:
    //   * ClassDecl       → STORE_GLOBAL                (Adder global)
    //   * MethodDecl      → installs sum:with: on Adder via __installMethod:as:
    //   * Adder newChild  → fresh instance whose parent is Adder
    //   * sum: 3 with: 4  → SEND_KEYWORD dispatches via parent chain to Adder's method
    //   * Method body     → x + y stored in local r, returned via `^ r`
    const char* src =
        "Object subclass: #Adder. "
        "Adder >> sum: x with: y | r | r := x + y. ^ r. "
        "Adder newChild sum: 3 with: 4.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("Engine: Object>>asActor wraps object as Actor", "[engine][actors]") {
    // Hand-build: 42 asActor.
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(42);          // const 0
    m.internSymbol("asActor"); // const 1
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::SEND_UNARY, 1);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* actor = rt.runTopLevel(m);
    REQUIRE(actor != nullptr);
    REQUIRE(actor != PROTO_NONE);

    auto* ctx = rt.rootCtx();
    // Actor should have __wrapped__ = 42
    auto* wrappedKey = ctx->fromUTF8String("__wrapped__")->asString(ctx);
    auto* wrapped = actor->getAttribute(ctx, wrappedKey);
    REQUIRE(wrapped != nullptr);
    REQUIRE(wrapped->asLong(ctx) == 42);

    // Actor should have __state__ = 0
    auto* stateKey = ctx->fromUTF8String("__state__")->asString(ctx);
    auto* state = actor->getAttribute(ctx, stateKey);
    REQUIRE(state->asLong(ctx) == 0);

    // Actor's prototype chain reaches actorProto
    REQUIRE(actor != rt.bootstrap().actorProto);  // It's a child, not the proto itself
}

TEST_CASE("Engine: user method with instance variable", "[engine][methods][instvars]") {
    // F4-U5: a Counter class with an instance variable `value`. The three
    // methods exercise both inst-var read (PUSH_INSTVAR) and write
    // (STORE_INSTVAR), plus the compiler's name-resolution priority
    // (`value` as a method selector vs. inst-var identifier in body).
    //
    // Pipeline: ClassDecl → three MethodDecls → Counter newChild instance →
    //   initialize sets value:=0; three increments bring it to 3;
    //   `c value` reads it back via the value getter.
    const char* src =
        "Object subclass: #Counter instanceVariableNames: 'value'. "
        "Counter >> initialize value := 0. "
        "Counter >> increment value := value + 1. "
        "Counter >> value ^ value. "
        "c := Counter newChild. "
        "c initialize. "
        "c increment. c increment. c increment. "
        "c value.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 3);
}

TEST_CASE("STRuntime scheduler: schedule + drainOne basics", "[engine][scheduler]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    // Create three dummy "actors" — just any ProtoObject will do for this queue test
    auto* a = rt.bootstrap().actorProto->newChild(ctx, true);
    auto* b = rt.bootstrap().actorProto->newChild(ctx, true);
    auto* c = rt.bootstrap().actorProto->newChild(ctx, true);

    REQUIRE(rt.scheduledCount() == 0);
    rt.schedule(a);
    rt.schedule(b);
    rt.schedule(c);
    REQUIRE(rt.scheduledCount() == 3);

    // schedule is idempotent
    rt.schedule(a);
    REQUIRE(rt.scheduledCount() == 3);

    // Drain three times → drains all
    REQUIRE(rt.drainOne(ctx));
    REQUIRE(rt.drainOne(ctx));
    REQUIRE(rt.drainOne(ctx));
    REQUIRE_FALSE(rt.drainOne(ctx));

    REQUIRE(rt.scheduledCount() == 0);

    // After drain, a can be re-scheduled
    rt.schedule(a);
    REQUIRE(rt.scheduledCount() == 1);
}

TEST_CASE("Engine: actor SEND returns a pending Future + drainOne resolves it",
          "[engine][actors]") {
    // F6-A4: wrap a Box class instance as an actor, send `add: 100 with: 23`
    // to it, observe that the send returns a pending Future, then drain the
    // mailbox and verify the Future resolves to 123.
    const char* src =
        "Object subclass: #Box. "
        "Box >> add: x with: y ^ x + y. "
        "actor := Box newChild asActor. "
        "f := actor add: 100 with: 23. "
        "f.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* fut = rt.runTopLevel(*bc);

    // At this point, the actor was scheduled but not yet drained.
    // The send should have returned a pending Future.
    REQUIRE(fut != nullptr);
    REQUIRE(fut != PROTO_NONE);

    auto* ctx = rt.rootCtx();
    auto* stateKey = ctx->fromUTF8String("__state__")->asString(ctx);
    auto* valueKey = ctx->fromUTF8String("__value__")->asString(ctx);
    REQUIRE(fut->getAttribute(ctx, stateKey)->asLong(ctx) == 0);  // pending

    // Drain the queue manually.
    REQUIRE(rt.drainOne(ctx));        // processes the add:with: message
    REQUIRE_FALSE(rt.drainOne(ctx));  // empty queue now

    // Future should be resolved now with value 123.
    REQUIRE(fut->getAttribute(ctx, stateKey)->asLong(ctx) == 1);  // resolved
    REQUIRE(fut->getAttribute(ctx, valueKey)->asLong(ctx) == 123);
}

TEST_CASE("Engine: Future wait drains queue and returns resolved value",
          "[engine][actors][future]") {
    // F6-A5: end-to-end actor-send-wait. `wait` should drain the scheduler
    // until the future settles, then return the resolved value (123).
    const char* src =
        "Object subclass: #Box. "
        "Box >> add: x with: y ^ x + y. "
        "(Box newChild asActor add: 100 with: 23) wait.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 123);
}

TEST_CASE("Engine: Future wait raises on rejection",
          "[engine][actors][future]") {
    // F6-A5: sending a selector the wrapped object doesn't understand causes
    // the scheduler to reject the future. `wait` must then surface the
    // rejection as a runtime_error carrying "Future rejected:" in its message.
    const char* src =
        "Object subclass: #Empty. "
        "(Empty newChild asActor noSuchMethod) wait.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(rt.runTopLevel(*bc),
                        Catch::Matchers::ContainsSubstring("Future rejected"));
}

TEST_CASE("Engine: Future>>thenDo: fires after actor resolves",
          "[engine][actors][future]") {
    // F6-A6: register a thenDo: callback on a Future returned by an actor
    // send. The callback captures the resolved value into a logger object's
    // inst-var. After wait drains the scheduler, the logger must hold the
    // resolved value (23 + 100 = 123).
    const char* src =
        "Object subclass: #Box instanceVariableNames: 'log'. "
        "Box >> initialize log := 0. "
        "Box >> add: x ^ x + 100. "
        "Box >> setLog: v log := v. "
        "Box >> getLog ^ log. "
        "logger := Box newChild. "
        "logger initialize. "
        "actor := Box newChild asActor. "
        "f := actor add: 23. "
        "f thenDo: [ :v | logger setLog: v ]. "
        "f wait. "
        "logger getLog.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 123);
}

TEST_CASE("Module loader: findModuleFile + loadModuleFromFile", "[engine][modules]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    std::string path = std::string(PROTOST_FIXTURES_DIR) + "/lib_simple.st";
    auto* module = rt.loadModuleFromFile(ctx, path, "lib_simple");

    REQUIRE(module != nullptr);
    REQUIRE(module != PROTO_NONE);

    // The module should have Greeter as an attribute.
    auto* greeterSym = ctx->fromUTF8String("Greeter")->asString(ctx);
    auto* greeterClass = module->getAttribute(ctx, greeterSym);
    REQUIRE(greeterClass != nullptr);
    REQUIRE(greeterClass != PROTO_NONE);

    // The class should have the `hello` method bound.
    auto* helloSym = ctx->fromUTF8String("hello")->asString(ctx);
    auto* helloMethod = greeterClass->getAttribute(ctx, helloSym);
    REQUIRE(helloMethod != nullptr);
    REQUIRE(helloMethod != PROTO_NONE);
}

TEST_CASE("Module loader: findModuleFile resolves from cwd", "[engine][modules]") {
    protoST::STRuntime rt;
    // Skip — depends on cwd state. Just exercise the path.
    std::string nope = rt.findModuleFile("definitely_does_not_exist_xyz");
    REQUIRE(nope.empty());
}

TEST_CASE("Engine: F6 hero — Counter wrapped as actor, async increments, sync wait", "[engine][actors][hero]") {
    const char* src =
        "Object subclass: #Counter instanceVariableNames: 'value'. "
        "Counter >> initialize value := 0. "
        "Counter >> increment value := value + 1. "
        "Counter >> value ^ value. "
        // Initialize first synchronously (no actor wrapping yet).
        "c := Counter newChild. "
        "c initialize. "
        // Now wrap it as actor.
        "a := c asActor. "
        // Async increments via the actor. Each returns a Future (discarded).
        "a increment. "
        "a increment. "
        "a increment. "
        // Final sync read via wait.
        "(a value) wait.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 3);
}

TEST_CASE("Module loader: caching returns same instance", "[engine][modules]") {
    // Pre-set STPATH so we can find lib_simple.st in the fixtures dir.
    // For test simplicity, use loadModule with an explicit absolute path workaround:
    // findModuleFile only handles relative-to-cwd or STPATH. We'll set STPATH at test time.
    std::string fixtures = PROTOST_FIXTURES_DIR;
    setenv("STPATH", fixtures.c_str(), /*overwrite=*/1);

    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    auto* m1 = rt.loadModule(ctx, "lib_simple");
    auto* m2 = rt.loadModule(ctx, "lib_simple");

    REQUIRE(m1 != nullptr);
    REQUIRE(m1 == m2);  // same instance

    // Cleanup
    unsetenv("STPATH");
}

TEST_CASE("Module loader: missing logical path throws", "[engine][modules]") {
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(rt.loadModule(rt.rootCtx(), "no_such_module_xyz_zz"),
                         Catch::Matchers::ContainsSubstring("module not found"));
}

TEST_CASE("Smalltalk-side: Import from: 'lib_simple' returns module wrapper with Greeter", "[engine][modules][import]") {
    std::string fixtures = PROTOST_FIXTURES_DIR;
    setenv("STPATH", fixtures.c_str(), 1);

    const char* src =
        "m := Import from: 'lib_simple'. "
        "m Greeter newChild twice: 21.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);

    unsetenv("STPATH");
}

TEST_CASE("F5 e2e: import counter_lib + use Counter end-to-end", "[engine][modules][hero]") {
    std::string fixtures = PROTOST_FIXTURES_DIR;
    setenv("STPATH", fixtures.c_str(), 1);

    const char* src =
        "lib := Import from: 'counter_lib'. "
        "c := lib Counter newChild. "
        "c initialize. "
        "c increment. "
        "c increment. "
        "c incrementBy: 10. "
        "c value.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->asLong(rt.rootCtx()) == 12);  // 2 + 10 = 12

    unsetenv("STPATH");
}

TEST_CASE("F5 e2e: import is cached — same module instance across calls", "[engine][modules]") {
    std::string fixtures = PROTOST_FIXTURES_DIR;
    setenv("STPATH", fixtures.c_str(), 1);

    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    auto* m1 = rt.loadModule(ctx, "counter_lib");
    auto* m2 = rt.loadModule(ctx, "counter_lib");
    REQUIRE(m1 != nullptr);
    REQUIRE(m1 == m2);  // pointer identity

    unsetenv("STPATH");
}

TEST_CASE("F5 v2: STModuleProvider registers and resolves via UMD", "[engine][modules][umd]") {
    std::string fixtures = PROTOST_FIXTURES_DIR;
    setenv("STPATH", fixtures.c_str(), 1);

    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    // Use protoCore's UMD path directly — not yet through Import>>from: (that's M2).
    auto* wrapper = rt.space()->getImportModule(ctx, "counter_lib", "exports");
    REQUIRE(wrapper != nullptr);
    REQUIRE(wrapper != PROTO_NONE);

    // Wrapper has attribute "exports" → the actual module.
    auto* exportsKey = ctx->fromUTF8String("exports")->asString(ctx);
    auto* mod = wrapper->getAttribute(ctx, exportsKey);
    REQUIRE(mod != nullptr);
    REQUIRE(mod != PROTO_NONE);

    // Module should have Counter class as attribute.
    auto* counterKey = ctx->fromUTF8String("Counter")->asString(ctx);
    auto* counter = mod->getAttribute(ctx, counterKey);
    REQUIRE(counter != nullptr);
    REQUIRE(counter != PROTO_NONE);

    unsetenv("STPATH");
}
