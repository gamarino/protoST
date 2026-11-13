// A closure's code (`__bc_ptr__`) is a protoCore external pointer to its
// BytecodeModule (0.10.0, src/runtime/BlockCode.h). It was the module's
// address stored as an integer, so any integer under that key was taken for
// code and dereferenced. These tests forge one and require that nothing runs
// it.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

namespace {

// The modules run here. A closure or method borrows its module (BlockCode.h),
// so a method one snippet defines needs that module while a later one sends it.
std::vector<std::unique_ptr<protoST::BytecodeModule>> g_modules;

const proto::ProtoObject* runSrc(protoST::STRuntime& rt, const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    g_modules.push_back(C.compileModule(*ast));
    REQUIRE(!C.hasErrors());
    return rt.runTopLevel(*g_modules.back());
}

const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* s) {
    return proto::ProtoString::createSymbol(ctx, s);
}

// A BlockClosure whose `__bc_ptr__` is an integer: under the old encoding,
// the address of a BytecodeModule.
const proto::ProtoObject* forgedClosure(protoST::STRuntime& rt) {
    auto* ctx = rt.rootCtx();
    auto* o = rt.bootstrap().blockProto->newChild(ctx, /*isMutable=*/true);
    o->setAttribute(ctx, rt.bootstrap().sym.bcPtr, ctx->fromLong(0x1000));
    return o;
}

} // namespace

TEST_CASE("An integer under __bc_ptr__ is never run as code", "[closure][block-code]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    runSrc(rt, "Object subclass: #Forged.");
    const proto::ProtoObject* cls = rt.globals()->getAttribute(ctx, sym(ctx, "Forged"));
    REQUIRE(cls != PROTO_NONE);
    const proto::ProtoObject* forged = forgedClosure(rt);
    cls->setAttribute(ctx, sym(ctx, "asMethod"), forged);
    cls->setAttribute(ctx, sym(ctx, "asMethod:"), forged);
    rt.globals()->setAttribute(ctx, sym(ctx, "ForgedBlock"), forged);

    SECTION("installed under a selector it is a value, not a method") {
        REQUIRE(runSrc(rt, "Forged new asMethod.") == forged);
        auto* r = runSrc(rt, "[Forged new asMethod: 1. #ran] on: Error do: [:e | #refused].");
        REQUIRE(r->asString(ctx)->toStdString(ctx) == "refused");
        REQUIRE(runSrc(rt, "Forged new respondsTo: #asMethod.") == PROTO_FALSE);
        REQUIRE(runSrc(rt, "Forged canUnderstand: #asMethod.") == PROTO_FALSE);
        auto* p = runSrc(rt,
            "[Forged new perform: #asMethod: with: 1. #ran] "
            "on: MessageNotUnderstood do: [:e | #notUnderstood].");
        REQUIRE(p->asString(ctx)->toStdString(ctx) == "notUnderstood");
    }

    SECTION("as a block it is not evaluated") {
        auto* r = runSrc(rt, "[ForgedBlock value. #ran] on: Error do: [:e | #refused].");
        REQUIRE(r->asString(ctx)->toStdString(ctx) == "refused");
        auto* c = runSrc(rt, "[#(1 2) collect: ForgedBlock. #ran] on: Error do: [:e | #refused].");
        REQUIRE(c->asString(ctx)->toStdString(ctx) == "refused");
    }

    SECTION("a BlockClosure without code is refused, not re-sent value") {
        auto* r = runSrc(rt, "[BlockClosure new value. #ran] on: Error do: [:e | #refused].");
        REQUIRE(r->asString(ctx)->toStdString(ctx) == "refused");
    }

    SECTION("a BlockClosure subclass that overrides value: is evaluated by its method") {
        runSrc(rt, "BlockClosure subclass: #MyBlock.\nMyBlock >> value: x ^ x * 10.");
        REQUIRE(runSrc(rt, "(#(1 2 3) collect: MyBlock new) last.")->asLong(ctx) == 30);
        // Its value, which it does not override, is BlockClosure's: refused.
        auto* r = runSrc(rt, "[MyBlock new value. #ran] on: Error do: [:e | e messageText].");
        REQUIRE(r->asString(ctx)->toStdString(ctx) == "the block has no code to run");
    }

    SECTION("closures share their code cell only when the runtime owns the module") {
        // An embedder-owned module (runTopLevel) may be freed while the
        // runtime lives: each closure gets a cell of its own, and nothing
        // is anchored for the module.
        runSrc(rt, "Forged >> adder: i ^ [:y | y + i].");
        const proto::ProtoString* bcKey = rt.bootstrap().sym.bcPtr;
        const proto::ProtoObject* a = runSrc(rt, "Forged new adder: 1.");
        const proto::ProtoObject* b = runSrc(rt, "Forged new adder: 2.");
        REQUIRE(a->getOwnAttributeDirect(ctx, bcKey) != b->getOwnAttributeDirect(ctx, bcKey));
        // An adopted module: one cell for every closure over the sub-block.
        protoST::Parser P("Forged >> doubler: i ^ [:y | y * i].");
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        REQUIRE(!C.hasErrors());
        rt.runTopLevel(rt.adoptModule(std::move(bc)));
        const proto::ProtoObject* c = runSrc(rt, "Forged new doubler: 3.");
        const proto::ProtoObject* d = runSrc(rt, "Forged new doubler: 4.");
        REQUIRE(c->getOwnAttributeDirect(ctx, bcKey) == d->getOwnAttributeDirect(ctx, bcKey));
        REQUIRE(runSrc(rt, "(Forged new doubler: 4) value: 5.")->asLong(ctx) == 20);
    }

    SECTION("real closures and methods still run") {
        REQUIRE(runSrc(rt, "[:x | x * 2] value: 21.")->asLong(ctx) == 42);
        runSrc(rt, "Forged >> twice: x ^ x * 2.");
        REQUIRE(runSrc(rt, "Forged new twice: 4.")->asLong(ctx) == 8);
        REQUIRE(runSrc(rt, "(#(1 2 3) collect: [:x | x + 1]) last.")->asLong(ctx) == 4);
        REQUIRE(runSrc(rt, "Forged new respondsTo: #twice:.") == PROTO_TRUE);
    }
}
