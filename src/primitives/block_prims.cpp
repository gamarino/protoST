#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ExecutionEngine.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>

namespace protoST {

// Real BlockClosure>>value implementation. PUSH_BLOCK attaches the address of
// the sub-module to the closure as a SmallInteger under attribute "__bc_ptr__";
// here we retrieve it, validate the arg count and run the sub-module with the
// supplied arguments pre-loaded into locals 0..argc-1.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc) {
    static const proto::ProtoString* bcKey =
        proto::ProtoString::createSymbol(ctx, "__bc_ptr__");
    static const proto::ProtoString* capKey =
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
    static const proto::ProtoString* homeKey =
        proto::ProtoString::createSymbol(ctx, "__home_frame__");
    unsigned long homeFrameId = 0;
    const proto::ProtoObject* homeObj = block->getAttribute(ctx, homeKey);
    if (homeObj && homeObj != PROTO_NONE)
        homeFrameId = static_cast<unsigned long>(homeObj->asLong(ctx));

    ExecutionEngine eng(rt);
    return eng.runWithArgs(ctx, *sub, /*self=*/PROTO_NONE, args, argc,
                           capDict, homeFrameId);
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
}

} // namespace protoST
