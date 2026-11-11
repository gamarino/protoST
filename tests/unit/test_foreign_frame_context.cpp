// K18 — a send into protoST from another runtime's frame context must leave
// that context's slots alone.
//
// Another runtime that runs in protoST's space (one space per process) gives
// each interpreted frame a ProtoContext of its own, chained to its caller, and
// keeps the frame's locals in that context's automaticLocals. When such a frame
// sends back into protoST (`__st_send__`), the caller's context belongs to
// protoST's space, so foreignSendEntry used to run the message on it directly.
// protoST's engine then treated the frame's automaticLocals as its own:
//   * it grew the array to its engine geometry (resizeAutomaticLocals, 4096
//     slots and up), which reallocates it under a runtime that may hold a raw
//     pointer to its locals;
//   * transient pins went into the scratch slots [0, 256) at the same indices
//     as the frame's locals, and a pin's release wrote PROTO_NONE over them.
//
// The case below is that frame: four locals holding sentinels and a raw
// pointer to them, then a send into protoST that nests `collect:` 300 deep
// (more pins than the scratch region holds). Afterwards the frame's locals
// must be its own sentinels, at the same address, and the walk's answer right.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

namespace {

const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* s) {
    return proto::ProtoString::createSymbol(ctx, s);
}

const proto::ProtoObject* send(proto::ProtoContext* ctx, const proto::ProtoObject* recv,
                               const char* selector,
                               std::vector<const proto::ProtoObject*> args = {}) {
    std::vector<const proto::ProtoObject*> a;
    a.push_back(reinterpret_cast<const proto::ProtoObject*>(sym(ctx, selector)));
    a.insert(a.end(), args.begin(), args.end());
    return recv->call(ctx, nullptr, sym(ctx, "__st_send__"), recv, a.data(),
                      static_cast<unsigned>(a.size()));
}

constexpr int kDepth = 300;
constexpr unsigned kFrameLocals = 4;

const proto::ProtoObject* g_walkerClass = nullptr;
bool g_localsKept = false;
bool g_localsInPlace = false;
long g_depth = -1;

// The other runtime's frame: its own context with four locals, then a send
// back into protoST.
const proto::ProtoObject* foreignFrame(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                       const proto::ParentLink*, const proto::ProtoArguments*,
                                       const proto::ProtoKeywordArguments*) {
    proto::ProtoContext frame(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr, kFrameLocals);
    const proto::ProtoObject* sentinels[kFrameLocals];
    for (unsigned i = 0; i < kFrameLocals; ++i) {
        sentinels[i] = frame.newObject(false);
        frame.setAutomaticLocal(i, sentinels[i]);
    }
    const proto::ProtoObject** locals = frame.getAutomaticLocals();

    const proto::ProtoObject* walker = send(&frame, g_walkerClass, "new");
    const proto::ProtoObject* nested =
        send(&frame, walker, "wrap:around:", {frame.fromInteger(kDepth), frame.fromInteger(1)});
    const proto::ProtoObject* walked = send(&frame, walker, "walk:", {nested});
    g_depth = send(&frame, walker, "depthOf:", {walked})->asLong(&frame);

    g_localsInPlace = frame.getAutomaticLocals() == locals;
    g_localsKept = true;
    for (unsigned i = 0; i < kFrameLocals; ++i)
        if (frame.getAutomaticLocal(i) != sentinels[i]) g_localsKept = false;
    return PROTO_NONE;
}

}  // namespace

TEST_CASE("K18: a send from another runtime's frame context leaves its slots alone",
          "[umd][interop][pin]") {
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
    run("Object subclass: #Walker.\n"
        "Walker >> wrap: n around: seed\n"
        "  | v | v := seed. n timesRepeat: [v := Array with: v]. ^ v.\n"
        "Walker >> walk: v\n"
        "  v isInteger ifTrue: [^ v + 1].\n"
        "  ^ v collect: [:each | self walk: each].\n"
        "Walker >> depthOf: v\n"
        "  | x d | x := v. d := 0.\n"
        "  [x isInteger] whileFalse: [x := x first. d := d + 1].\n"
        "  ^ d.\n"
        "Walker >> call: aCallable\n"
        "  ^ aCallable apply().\n"
        "nil.");
    proto::ProtoContext* root = rt.rootCtx();
    g_walkerClass = rt.globals()->getAttribute(root, sym(root, "Walker"));
    REQUIRE(g_walkerClass != PROTO_NONE);

    const proto::ProtoObject* callable = root->newObject(true);
    callable->setAttribute(root, sym(root, "apply"), root->fromMethod(nullptr, &foreignFrame));
    const proto::ProtoObject* walker = send(root, g_walkerClass, "new");
    send(root, walker, "call:", {callable});

    CHECK(g_depth == kDepth);
    CHECK(g_localsKept);
    CHECK(g_localsInPlace);
}

namespace {

// Another runtime's function that runs a protoST block it was handed: it sends
// `value` to the block through __st_send__, on the context it was called with
// (protoST's own engine context, so the send takes the path of a caller in
// protoST's own space).
const proto::ProtoObject* runBlockBack(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                       const proto::ParentLink*,
                                       const proto::ProtoArguments* positional,
                                       const proto::ProtoKeywordArguments*) {
    const proto::ProtoObject* block = positional->getAt(ctx, 0);
    return send(ctx, block, "value");
}

}  // namespace

// The block's `^ 42` returns from `escape:`, whose frame is above the foreign
// function's. Until fix round 2 of K15 the send from a caller in protoST's own
// space skipped the runtime boundary, and the NonLocalReturn unwound through
// the foreign function's frames (escape: answered 42). Like a send from a
// foreign space, it must be refused: the foreign function sees an error, which
// re-enters protoST as an Error where the function returns.
TEST_CASE("K18: a non-local return cannot leave a __st_send__ through the caller's frames",
          "[umd][interop]") {
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
    run("Object subclass: #Escaper.\n"
        "Escaper >> escape: aCallable\n"
        "  ^ [aCallable apply([^ 42]). 0] on: Error do: [:e | e messageText].\n"
        "nil.");
    proto::ProtoContext* root = rt.rootCtx();
    const proto::ProtoObject* cls = rt.globals()->getAttribute(root, sym(root, "Escaper"));
    REQUIRE(cls != PROTO_NONE);
    const proto::ProtoObject* callable = root->newObject(true);
    callable->setAttribute(root, sym(root, "apply"), root->fromMethod(nullptr, &runBlockBack));
    const proto::ProtoObject* r = send(root, send(root, cls, "new"), "escape:", {callable});

    REQUIRE(r != nullptr);
    INFO("escape: answered " << (r->isInteger(root) ? std::to_string(r->asLong(root))
                                                    : std::string("a non-integer")));
    REQUIRE(r->asString(root) != nullptr);
    CHECK(r->asString(root)->toStdString(root).find("control-flow unwind") != std::string::npos);
}
