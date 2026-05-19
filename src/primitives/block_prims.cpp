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
        ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
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
    ExecutionEngine eng(rt);
    return eng.runWithArgs(ctx, *sub, /*self=*/PROTO_NONE, args, argc);
}

namespace {

const proto::ProtoObject* prim_Block_value(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    return invokeBlock(rt, ctx, r, a, argc);
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
}

} // namespace protoST
