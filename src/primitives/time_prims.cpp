#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <chrono>

namespace protoST {

// T4-e — wall-clock access (Track 4, sub-slice e).
//
// Two low-level clock hooks bound on `objectProto`, the C++ floor under the
// loadable `lib/time.st` module. They are intentionally low-level — they
// answer a bare integer count of milliseconds — and the idiomatic `Time` /
// `Timestamp` / `Duration` object model is built in pure protoST on top.
//
//   * `__currentMillis`   — milliseconds since the Unix epoch, from
//     `std::chrono::system_clock`. This is the wall clock: it is the basis of
//     `Time now` and is comparable across processes, but it is NOT monotonic
//     (it can jump on an NTP step or a manual clock change).
//   * `__monotonicMillis` — milliseconds from `std::chrono::steady_clock`,
//     whose epoch is unspecified. Only differences are meaningful; the clock
//     never goes backwards, so it is the right basis for `millisecondsToRun:`
//     and any elapsed-time measurement.
//
// Both values comfortably fit a 56-bit SmallInteger: epoch milliseconds in
// 2026 are ~1.8e12, far inside 2^55, so no LargeInteger promotion is needed.

namespace {

// `__currentMillis` — epoch milliseconds from the system (wall) clock.
const proto::ProtoObject* prim_CurrentMillis(STRuntime&,
                                             proto::ProtoContext* ctx,
                                             const proto::ProtoObject*,
                                             const proto::ProtoObject* const*,
                                             int) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now);
    return ctx->fromLong(static_cast<long long>(ms.count()));
}

// `__monotonicMillis` — milliseconds from the steady (monotonic) clock. The
// epoch is unspecified; only differences between two readings are meaningful.
const proto::ProtoObject* prim_MonotonicMillis(STRuntime&,
                                               proto::ProtoContext* ctx,
                                               const proto::ProtoObject*,
                                               const proto::ProtoObject* const*,
                                               int) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now);
    return ctx->fromLong(static_cast<long long>(ms.count()));
}

} // anon

void installTimePrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    // Bound on `objectProto` so the `lib/time.st` module — whose classes all
    // descend from Object — can reach them as plain message sends.
    bindPrimitive(rt, b.objectProto, "__currentMillis",
                  reg.registerPrim(prim_CurrentMillis));
    bindPrimitive(rt, b.objectProto, "__monotonicMillis",
                  reg.registerPrim(prim_MonotonicMillis));
}

} // namespace protoST
