// K18 — re-entry through another runtime's frames.
//
// Written by the K15 re-review (2026-11-11). protoST calls a foreign function
// that opens a frame context with sentinel locals and sends back into protoST,
// which calls the function again: three levels, a 300-deep collect: walk at
// each, and at the innermost a protoST error that leaves the entry. Every
// level must keep its frame's locals and get the right walk, and afterwards
// the pin cursor, the overflow chain and the engine count must be back where
// they started, with protoST still working on its own context.
#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/TransientPin.h"
#include "runtime/ExecutionEngine.h"
#include "protoCore.h"
#include <memory>
#include <string>
#include <vector>

namespace {
const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* s) {
    return proto::ProtoString::createSymbol(ctx, s);
}
const proto::ProtoObject* send(proto::ProtoContext* ctx, const proto::ProtoObject* recv,
                               const char* selector, std::vector<const proto::ProtoObject*> args = {}) {
    std::vector<const proto::ProtoObject*> a;
    a.push_back(reinterpret_cast<const proto::ProtoObject*>(sym(ctx, selector)));
    a.insert(a.end(), args.begin(), args.end());
    return recv->call(ctx, nullptr, sym(ctx, "__st_send__"), recv, a.data(), static_cast<unsigned>(a.size()));
}
const proto::ProtoObject* g_walker = nullptr;
const proto::ProtoObject* g_callable = nullptr;
int g_level = 0;
int g_okLevels = 0;
bool g_errorSeen = false;

// Frame of another runtime: sentinel locals; recurses into protoST (which
// calls this frame again) three levels deep, does a deep walk at each level,
// and at the innermost level provokes an error in protoST.
const proto::ProtoObject* frameFn(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                  const proto::ParentLink*, const proto::ProtoArguments*,
                                  const proto::ProtoKeywordArguments*) {
    proto::ProtoContext frame(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr, 3);
    const proto::ProtoObject* s[3];
    for (int i = 0; i < 3; ++i) { s[i] = frame.newObject(false); frame.setAutomaticLocal(i, s[i]); }
    const int level = ++g_level;
    const unsigned cursorIn = protoST::g_scratchCursor;
    const proto::ProtoObject* nested =
        send(&frame, g_walker, "wrap:around:", {frame.fromInteger(300), frame.fromInteger(level)});
    if (level < 3) {
        send(&frame, g_walker, "call:", {g_callable});
    } else {
        try { send(&frame, g_walker, "noSuchMessageK18"); }
        catch (...) { g_errorSeen = true; }
    }
    const proto::ProtoObject* walked = send(&frame, g_walker, "walk:", {nested});
    const long long d = send(&frame, g_walker, "depthOf:", {walked})->asLong(&frame);
    const long long leaf = send(&frame, g_walker, "leafOf:", {walked})->asLong(&frame);
    bool kept = true;
    for (int i = 0; i < 3; ++i) if (frame.getAutomaticLocal(i) != s[i]) kept = false;
    if (kept && d == 300 && leaf == level + 1 && protoST::g_scratchCursor == cursorIn) ++g_okLevels;
    --g_level;
    return PROTO_NONE;
}
}  // namespace

TEST_CASE("K18: nested foreign frames, an error inside, cursors restored", "[umd][interop][pin]") {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;
    auto run = [&](const std::string& src) {
        protoST::Parser P(src.c_str());
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        REQUIRE(!C.hasErrors());
        const proto::ProtoObject* r = rt.runTopLevel(*bc);
        kept.push_back(std::move(bc));
        return r;
    };
    run("Object subclass: #W.\n"
        "W >> wrap: n around: seed\n"
        "  | v | v := seed. n timesRepeat: [v := Array with: v]. ^ v.\n"
        "W >> walk: v\n"
        "  v isInteger ifTrue: [^ v + 1].\n"
        "  ^ v collect: [:each | self walk: each].\n"
        "W >> depthOf: v\n"
        "  | x d | x := v. d := 0.\n"
        "  [x isInteger] whileFalse: [x := x first. d := d + 1].\n"
        "  ^ d.\n"
        "W >> leafOf: v\n"
        "  | x | x := v. [x isInteger] whileFalse: [x := x first]. ^ x.\n"
        "W >> call: aCallable\n"
        "  ^ aCallable apply().\n"
        "nil.");
    proto::ProtoContext* root = rt.rootCtx();
    g_walker = send(root, rt.globals()->getAttribute(root, sym(root, "W")), "new");
    g_callable = root->newObject(true);
    g_callable->setAttribute(root, sym(root, "apply"), root->fromMethod(nullptr, &frameFn));
    const std::size_t enginesBefore = protoST::ExecutionEngine::liveEnginesOnThisThread();
    send(root, g_walker, "call:", {g_callable});
    CHECK(g_errorSeen);
    CHECK(g_okLevels == 3);
    CHECK(protoST::g_scratchCursor == 0);
    CHECK(protoST::overflowPinsOnThisThread() == 0);
    CHECK(protoST::ExecutionEngine::liveEnginesOnThisThread() == enginesBefore);
    // protoST still works on its own context afterwards, deep.
    const proto::ProtoObject* r = run("(W new depthOf: (W new walk: (W new wrap: 400 around: 7))) + 0");
    CHECK(r->asLong(root) == 400);
}
