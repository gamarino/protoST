#include "protoST/STRuntime.h"
#include "runtime/NativeError.h"
#include "runtime/ListIndex.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BlockCode.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/NativeExceptionBridge.h"
#include "protoCore.h"

#include <stdexcept>
#include <vector>
#include <string>

namespace protoST {

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

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

// Real BlockClosure>>value implementation. PUSH_BLOCK attaches the sub-module
// to the closure as an external pointer under attribute "__bc_ptr__" (BlockCode.h);
// here we retrieve it, validate the arg count and run the sub-module with the
// supplied arguments pre-loaded into locals 0..argc-1.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc) {
    // F6 v5 hot-path fix (2026-05-23): the 4 createSymbol calls in this
    // function were a major contention point — invokeBlock runs ~100 K
    // times in mt100a (one per inner-loop iteration), each previously
    // costing 4 SymbolTable shard-lock lookups (~400 K total). All four
    // hot keys are now pulled from the Bootstrap cache.
    const proto::ProtoString* bcKey  = rt.bootstrap().sym.bcPtr;
    const proto::ProtoString* capKey = rt.bootstrap().sym.captured;
    const BytecodeModule* sub = blockCodeOf(ctx, block->getOwnAttributeDirect(ctx, bcKey));
    if (!sub) {
        // A BlockClosure without code (`BlockClosure new`, or a forged
        // `__bc_ptr__`) has nothing to run. Sending it value would find this
        // very primitive again and recurse until the stack overflowed.
        if (block->isInstanceOf(ctx, rt.bootstrap().blockProto) == PROTO_TRUE)
            throw NativeError("the block has no code to run");
        // Not a block: evaluate it the way Smalltalk does, by sending it
        // value / value: / value:value: (so a Symbol stands for a block:
        // #(1 2 3) collect: #squared).
        static const char* const kValue[] = { "value", "value:", "value:value:" };
        if (argc >= 0 && argc <= 2) {
            bool understood = false;
            const proto::ProtoObject* r = sendDynamic(
                rt, ctx, block, proto::ProtoString::createSymbol(ctx, kValue[argc]),
                args, argc, &understood);
            if (understood) return r ? r : PROTO_NONE;
        }
        throw NativeError("a block was expected here, but the argument is not a block");
    }
    if (sub->argCount() != argc) {
        throw NativeError(
            "block arg count mismatch (expected " +
            std::to_string(sub->argCount()) + ", got " +
            std::to_string(argc) + ")");
    }
    // F3: thread the captured dict through. PUSH_BLOCK in F3-C5 will install
    // it on the closure under "__captured__"; until then this read may return
    // nil/nullptr and the block runs with no captured environment.
    const proto::ProtoObject* capDict = block->getOwnAttributeDirect(ctx, capKey);
    if (capDict == PROTO_NONE) capDict = nullptr;

    // Track 1 slice 1: thread the block's home method activation through to
    // the nested engine's initial (block) frame. PUSH_BLOCK stamped
    // `__home_frame__` with the creating method's homeFrameId; an `^expr`
    // inside this block must return from THAT method, not from the block.
    // The nested engine cannot see the home frame (it lives in the parent
    // engine's frames_), so its RETURN handler throws a NonLocalReturn which
    // bubbles past invokeBlock to the parent engine's runLoop.
    const proto::ProtoString* homeKey = rt.bootstrap().sym.homeFrame;
    proto::proto_ulong homeFrameId = 0;
    const proto::ProtoObject* homeObj = block->getOwnAttributeDirect(ctx, homeKey);
    if (homeObj && homeObj != PROTO_NONE)
        homeFrameId = static_cast<proto::proto_ulong>(homeObj->asLong(ctx));

    // CLO Part 1: a block created inside a method inherits that method's
    // receiver as its `self`. PUSH_BLOCK stamps it onto the closure as
    // `__block_self__`; pass it as the block frame's self so `self` and
    // PUSH_INSTVAR inside the block resolve to the enclosing method's
    // receiver. Absent for blocks not built by PUSH_BLOCK — fall back to
    // PROTO_NONE.
    const proto::ProtoString* blkSelfKey = rt.bootstrap().sym.blockSelf;
    const proto::ProtoObject* blkSelf = block->getOwnAttributeDirect(ctx, blkSelfKey);
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
    const BytecodeModule* sub = blockCodeOf(ctx, block->getOwnAttributeDirect(ctx, bcKey));
    return sub ? sub->argCount() : -1;
}

namespace {

// aBlock numArgs → the number of arguments the block takes.
const proto::ProtoObject* prim_Block_numArgs(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const*, int) {
    return ctx->fromLong(blockArgCount(ctx, r));
}

// aBlock valueWithArguments: anArray → evaluate with the array's elements.
const proto::ProtoObject* prim_Block_valueWithArguments(STRuntime& rt, proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* r,
                                                        const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw NativeError("valueWithArguments: expects 1 arg");
    const proto::ProtoObject* data = a[0] && a[0] != PROTO_NONE
        ? a[0]->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__")) : nullptr;
    const proto::ProtoList* list = data && data != PROTO_NONE ? data->asList(ctx) : nullptr;
    if (!list) throw NativeError("valueWithArguments: expects an Array");
    std::vector<const proto::ProtoObject*> args;
    for (proto::proto_ulong i = 0; i < list->getSize(ctx); ++i)
        args.push_back(list->getAt(ctx, listIndex(i)));
    return invokeBlock(rt, ctx, r, args.data(), static_cast<int>(args.size()));
}

const proto::ProtoObject* prim_Block_value(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    return invokeBlock(rt, ctx, r, a, argc);
}

// The loop protocol of a block. The condition is the receiver's value; one
// that is not a Boolean goes through loopConditionIsTrue (a resumable
// NonBooleanReceiver, the resumed value true unless it is false), the rule of
// the inlined loops (ASSERT_LOOP_BOOL) too. Until 0.10.0 these primitives
// took any non-true value as false.
const proto::ProtoObject* prim_Block_whileTrue(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* a, int) {
    while (loopConditionIsTrue(rt, ctx, invokeBlock(rt, ctx, r, nullptr, 0), "whileTrue:"))
        invokeBlock(rt, ctx, a[0], nullptr, 0);
    return PROTO_NONE;
}

// D9: `whileFalse:` — the mirror of `whileTrue:`. The receiver is the
// condition block; the loop body runs while the condition yields false.
const proto::ProtoObject* prim_Block_whileFalse(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a, int) {
    while (!loopConditionIsTrue(rt, ctx, invokeBlock(rt, ctx, r, nullptr, 0), "whileFalse:"))
        invokeBlock(rt, ctx, a[0], nullptr, 0);
    return PROTO_NONE;
}

// D9: no-argument `whileTrue` / `whileFalse` — the receiver is both condition
// and body (it is re-evaluated each iteration and tested for the loop sense).
const proto::ProtoObject* prim_Block_whileTrueNoArg(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const*, int) {
    while (loopConditionIsTrue(rt, ctx, invokeBlock(rt, ctx, r, nullptr, 0), "whileTrue")) {}
    return PROTO_NONE;
}
const proto::ProtoObject* prim_Block_whileFalseNoArg(STRuntime& rt, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* r,
                                                      const proto::ProtoObject* const*, int) {
    while (!loopConditionIsTrue(rt, ctx, invokeBlock(rt, ctx, r, nullptr, 0), "whileFalse")) {}
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
    auto& b   = rt.bootstrap();
    const proto::ProtoMethod blockValue = primitiveMethod<prim_Block_value>;
    bindPrimitive(rt, b.blockProto, "numArgs", primitiveMethod<prim_Block_numArgs>);
    bindPrimitive(rt, b.blockProto, "valueWithArguments:",
                  primitiveMethod<prim_Block_valueWithArguments>);
    bindPrimitive(rt, b.blockProto, "value",                    blockValue);
    bindPrimitive(rt, b.blockProto, "value:",                   blockValue);
    bindPrimitive(rt, b.blockProto, "value:value:",             blockValue);
    bindPrimitive(rt, b.blockProto, "value:value:value:",       blockValue);
    bindPrimitive(rt, b.blockProto, "value:value:value:value:", blockValue);

    const proto::ProtoMethod whileTrue = primitiveMethod<prim_Block_whileTrue>;
    // Functional but not exercised by F2 tests — needs closures (F3+) for the
    // typical loop idiom over mutable counters.
    bindPrimitive(rt, b.blockProto, "whileTrue:", whileTrue);
    // D9: the rest of the block-side loop protocol.
    bindPrimitive(rt, b.blockProto, "whileFalse:",
                  primitiveMethod<prim_Block_whileFalse>);
    bindPrimitive(rt, b.blockProto, "whileTrue",
                  primitiveMethod<prim_Block_whileTrueNoArg>);
    bindPrimitive(rt, b.blockProto, "whileFalse",
                  primitiveMethod<prim_Block_whileFalseNoArg>);
    bindPrimitive(rt, b.blockProto, "repeat",
                  primitiveMethod<prim_Block_repeat>);
}

} // namespace protoST
