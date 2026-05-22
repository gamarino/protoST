#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ExecutionEngine.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>

namespace protoST {

// The `__bc_ptr__` attribute key, resolved once and shared by every reader of
// a block's metadata. Symbol interning is per ProtoSpace; this caches the
// symbol of whichever ProtoSpace first asks for it, and every subsequent
// reader (`invokeBlock`, `blockArgCount`) uses that same symbol object — so
// `invokeBlock` and `blockArgCount` never disagree about whether a block
// carries `__bc_ptr__`, even across the multi-runtime test harness.
static const proto::ProtoString* bcPtrKey(proto::ProtoContext* ctx) {
    const proto::ProtoString* key =
        proto::ProtoString::createSymbol(ctx, "__bc_ptr__");
    return key;
}

// Real BlockClosure>>value implementation. PUSH_BLOCK attaches the address of
// the sub-module to the closure as a SmallInteger under attribute "__bc_ptr__";
// here we retrieve it, validate the arg count and run the sub-module with the
// supplied arguments pre-loaded into locals 0..argc-1.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc) {
    const proto::ProtoString* bcKey = bcPtrKey(ctx);
    const proto::ProtoString* capKey =
        proto::ProtoString::createSymbol(ctx, "__captured__");
    auto* bcPtrObj = block->getAttribute(ctx, bcKey);
    if (!bcPtrObj || bcPtrObj == PROTO_NONE)
        throw std::runtime_error("block missing __bc_ptr__");
    const BytecodeModule* sub =
        reinterpret_cast<const BytecodeModule*>(bcPtrObj->asLong(ctx));
    if (sub->argCount() != argc) {
        throw std::runtime_error(
            "block arg count mismatch (expected " +
            std::to_string(sub->argCount()) + ", got " +
            std::to_string(argc) + ")");
    }
    // F3: thread the captured dict through. PUSH_BLOCK in F3-C5 will install
    // it on the closure under "__captured__"; until then this read may return
    // nil/nullptr and the block runs with no captured environment.
    const proto::ProtoObject* capDict = block->getAttribute(ctx, capKey);
    if (capDict == PROTO_NONE) capDict = nullptr;

    // Track 1 slice 1: thread the block's home method activation through to
    // the nested engine's initial (block) frame. PUSH_BLOCK stamped
    // `__home_frame__` with the creating method's homeFrameId; an `^expr`
    // inside this block must return from THAT method, not from the block.
    // The nested engine cannot see the home frame (it lives in the parent
    // engine's frames_), so its RETURN handler throws a NonLocalReturn which
    // bubbles past invokeBlock to the parent engine's runLoop.
    const proto::ProtoString* homeKey =
        proto::ProtoString::createSymbol(ctx, "__home_frame__");
    unsigned long homeFrameId = 0;
    const proto::ProtoObject* homeObj = block->getAttribute(ctx, homeKey);
    if (homeObj && homeObj != PROTO_NONE)
        homeFrameId = static_cast<unsigned long>(homeObj->asLong(ctx));

    // CLO Part 1: a block created inside a method inherits that method's
    // receiver as its `self`. PUSH_BLOCK stamps it onto the closure as
    // `__block_self__`; pass it as the block frame's self so `self` and
    // PUSH_INSTVAR inside the block resolve to the enclosing method's
    // receiver. Absent for blocks not built by PUSH_BLOCK — fall back to
    // PROTO_NONE.
    const proto::ProtoString* blkSelfKey =
        proto::ProtoString::createSymbol(ctx, "__block_self__");
    const proto::ProtoObject* blkSelf = block->getAttribute(ctx, blkSelfKey);
    if (!blkSelf || blkSelf == PROTO_NONE) blkSelf = PROTO_NONE;

    ExecutionEngine eng(rt);
    return eng.runWithArgs(ctx, *sub, /*self=*/blkSelf, args, argc,
                           capDict, homeFrameId);
}

// D9: report how many arguments a block closure expects, by reading the
// `__bc_ptr__` attribute stamped by PUSH_BLOCK. Returns -1 when `block` is not
// a block closure (no `__bc_ptr__`). Defined here, alongside `invokeBlock`, so
// the `__bc_ptr__` symbol is resolved through exactly one code path — callers
// in other translation units use this rather than re-deriving the symbol.
//
// The `__bc_ptr__` key comes from the shared `bcPtrKey` accessor, so this
// reader resolves the attribute through the very same symbol object as
// `invokeBlock` — the two can never disagree about a block's metadata.
int blockArgCount(proto::ProtoContext* ctx, const proto::ProtoObject* block) {
    if (!block || block == PROTO_NONE) return -1;
    const proto::ProtoString* bcKey = bcPtrKey(ctx);
    const proto::ProtoObject* bcPtrObj = block->getAttribute(ctx, bcKey);
    if (!bcPtrObj || bcPtrObj == PROTO_NONE) return -1;
    const BytecodeModule* sub =
        reinterpret_cast<const BytecodeModule*>(bcPtrObj->asLong(ctx));
    return sub->argCount();
}

namespace {

const proto::ProtoObject* prim_Block_value(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    return invokeBlock(rt, ctx, r, a, argc);
}

const proto::ProtoObject* prim_Block_whileTrue(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* a, int) {
    while (true) {
        auto* cond = invokeBlock(rt, ctx, r, nullptr, 0);
        if (cond != PROTO_TRUE) break;
        invokeBlock(rt, ctx, a[0], nullptr, 0);
    }
    return PROTO_NONE;
}

// D9: `whileFalse:` — the mirror of `whileTrue:`. The receiver is the
// condition block; the loop body runs while the condition yields false.
const proto::ProtoObject* prim_Block_whileFalse(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a, int) {
    while (true) {
        auto* cond = invokeBlock(rt, ctx, r, nullptr, 0);
        if (cond == PROTO_TRUE) break;
        invokeBlock(rt, ctx, a[0], nullptr, 0);
    }
    return PROTO_NONE;
}

// D9: no-argument `whileTrue` / `whileFalse` — the receiver is both condition
// and body (it is re-evaluated each iteration and tested for the loop sense).
const proto::ProtoObject* prim_Block_whileTrueNoArg(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const*, int) {
    while (invokeBlock(rt, ctx, r, nullptr, 0) == PROTO_TRUE) { /* loop */ }
    return PROTO_NONE;
}
const proto::ProtoObject* prim_Block_whileFalseNoArg(STRuntime& rt, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* r,
                                                      const proto::ProtoObject* const*, int) {
    while (invokeBlock(rt, ctx, r, nullptr, 0) != PROTO_TRUE) { /* loop */ }
    return PROTO_NONE;
}

// D9: `repeat` — evaluate the receiver block forever. The loop is exited only
// by a non-local return (`^`) inside the block.
const proto::ProtoObject* prim_Block_repeat(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    while (true) { invokeBlock(rt, ctx, r, nullptr, 0); }
    return PROTO_NONE;  // unreachable
}

} // anon

void installBlockPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    int idx = reg.registerPrim(prim_Block_value);
    bindPrimitive(rt, b.blockProto, "value",                    idx);
    bindPrimitive(rt, b.blockProto, "value:",                   idx);
    bindPrimitive(rt, b.blockProto, "value:value:",             idx);
    bindPrimitive(rt, b.blockProto, "value:value:value:",       idx);
    bindPrimitive(rt, b.blockProto, "value:value:value:value:", idx);

    int wIdx = reg.registerPrim(prim_Block_whileTrue);
    // Functional but not exercised by F2 tests — needs closures (F3+) for the
    // typical loop idiom over mutable counters.
    bindPrimitive(rt, b.blockProto, "whileTrue:", wIdx);
    // D9: the rest of the block-side loop protocol.
    bindPrimitive(rt, b.blockProto, "whileFalse:",
                  reg.registerPrim(prim_Block_whileFalse));
    bindPrimitive(rt, b.blockProto, "whileTrue",
                  reg.registerPrim(prim_Block_whileTrueNoArg));
    bindPrimitive(rt, b.blockProto, "whileFalse",
                  reg.registerPrim(prim_Block_whileFalseNoArg));
    bindPrimitive(rt, b.blockProto, "repeat",
                  reg.registerPrim(prim_Block_repeat));
}

} // namespace protoST
