#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "debugger/DebuggerRuntime.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

namespace protoST {

namespace {

const proto::ProtoObject* prim_DebuggerHalt(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject*,
                                             const proto::ProtoObject* const* a, int argc) {
    if (!rt.debugger().attached()) return PROTO_NONE;
    std::string reason = "user halt";
    if (argc >= 1 && a[0]) {
        // try to read as string; fall back if not a string
        try {
            auto* s = a[0]->asString(ctx);
            if (s) reason = s->toStdString(ctx);
        } catch (...) {}
    }
    throw DebuggerHalt(reason);
}

} // anon

void installDebuggerPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    int idx = reg.registerPrim(prim_DebuggerHalt);
    bindPrimitive(rt, b.objectProto, "halt",  idx);
    bindPrimitive(rt, b.objectProto, "halt:", idx);
}

} // namespace protoST
