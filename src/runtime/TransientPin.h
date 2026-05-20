// TransientPin.h — F6 v3 E5: synchronous GC root discipline for transient
// ProtoObject* values held in C++ locals across possible-GC operations.
//
// The bug class
// -------------
// protoCore runs a tracing, stop-the-world, concurrent-marking GC. It traces:
// ProtoContext::automaticLocals, closureLocals, the context chain, and
// registered ProtoRootSets. It does NOT trace raw C++ stack/heap memory.
//
// Any `ProtoObject*` held in a C++ local across an operation that may trigger
// GC is a use-after-free waiting to happen: when GC runs it reclaims anything
// not reachable from a traced root, and a bare C++ local pointer then dangles.
//
// Operations that may trigger GC (anything that allocates a Cell):
//   * ctx->newList / newSparseList / newTuple* / fromLong / fromDouble /
//     fromUTF8String / fromExternalPointer / newChild / clone / new(ctx)...
//   * appendLast / appendFirst / reverse / slice on a list
//   * setAttribute on a MUTABLE object (allocates a sparse-list node)
//   * getAttribute that runs a callback/descriptor
//   * createSymbol / string interning
//   * user-method dispatch / block invocation / sub-engine runs
//   * anything reaching allocCell → safepoint → potential STW GC
//
// The mechanism (no per-object ProtoRootSet handle — E2b principle)
// -----------------------------------------------------------------
// protoST already has a fully GC-traced flat slot array: the ExecutionEngine
// context's `automaticLocals`, pre-sized to ExecutionEngine::kSlotCapacity
// (8192) once before any opcode runs (F6 v3 E3). Frame regions are packed
// bottom-up from a thread-local cursor (`g_slotCursor`).
//
// E5 reserves a FIXED scratch region at the TOP of that same array —
// `[kSlotCapacity - kScratchSlots, kSlotCapacity)` — exclusively for transient
// pins. A separate thread-local cursor grows DOWNWARD from the top. Because
// the scratch region is part of `automaticLocals`, the GC traces it for free:
// no ProtoRootSet handle, no liveRegistry churn.
//
// Frame regions (bottom-up) and scratch pins (top-down) grow toward each
// other; pushFrame already rejects overflow past `kSlotCapacity - kScratchSlots`
// and TransientPin asserts it never grows past the frame cursor.
//
// Why a primitive can pin with only `ctx`
// ---------------------------------------
// A primitive receives `STRuntime& rt, ProtoContext* ctx, ...`. The `ctx` IS
// the engine context that invoked it — the engine sized it to kSlotCapacity
// before dispatching the primitive's SEND. The scratch region lives at a
// FIXED, absolute slot range in any engine context, so a primitive pins into
// the exact same range using just `ctx`. The scratch cursor is thread-local
// and grows LIFO across nested engines / primitives on the same C++ stack.
//
// Usage
// -----
//   const proto::ProtoObject* fut = rt.newFuture(ctx);
//   TransientPin pinFut(ctx, fut);            // GC-safe across the lines below
//   auto* msg = objectProto->newChild(ctx, true);
//   TransientPin pinMsg(ctx, msg);
//   auto* argsList = ctx->newList();
//   ... build / setAttribute / schedule ...
//   // pin destructors release the slots here (also on early return / throw)
//
// Non-copyable, non-movable: each pin is bound to a single C++ stack lifetime.
// Pins nest strictly LIFO (matching C++ scope nesting); the destructor asserts
// LIFO discipline in debug builds.
#ifndef PROTOST_TRANSIENT_PIN_H
#define PROTOST_TRANSIENT_PIN_H

#include "protoCore.h"

#include <cassert>

namespace protoST {

// Scratch-region geometry. Kept in sync with ExecutionEngine::kSlotCapacity
// (8192). The scratch region is the top `kScratchSlots` slots; frame regions
// occupy `[0, kSlotCapacity - kScratchSlots)`.
//
// 256 transient pins is far past any real nesting depth: the deepest pinning
// site (the actor-message SEND fast-path) pins ~6 objects, and primitives
// pin a handful each; even a long chain of nested engines / primitives on one
// C++ stack stays well under 256. Overflow is a hard error, never silent.
inline constexpr unsigned int kEngineSlotCapacity = 8192;
inline constexpr unsigned int kScratchSlots       = 256;
inline constexpr unsigned int kScratchBase        =
    kEngineSlotCapacity - kScratchSlots;        // first scratch slot index

// Thread-local scratch cursor. Counts slots CONSUMED in the scratch region;
// the next free scratch slot is `kEngineSlotCapacity - 1 - g_scratchCursor`,
// i.e. the region fills from the top downward. Thread-local because a
// ProtoContext is per-thread and several engines / primitives nest LIFO on
// the same OS thread.
//
// Defined in ExecutionEngine.cpp alongside the frame-region cursor so the two
// share a translation unit and an overflow assertion can compare them.
extern thread_local unsigned int g_scratchCursor;

// Highest value g_slotCursor (the frame-region cursor, defined in
// ExecutionEngine.cpp) is allowed to reach. Exposed so pushFrame can assert
// frame regions never collide with the scratch region.
inline constexpr unsigned int kFrameRegionLimit = kScratchBase;

// RAII pin for a `ProtoObject*` held in a C++ local across a possible-GC
// operation. Construction claims the next scratch slot in the engine
// context's GC-traced automaticLocals and writes the pointer there, so the
// tracing collector keeps the object alive. Destruction releases the slot.
//
// `ctx` must be the engine context (pre-sized to kEngineSlotCapacity). A
// nullptr `ctx` or nullptr `obj` makes the pin an inert no-op — kept cheap so
// callers that may legitimately receive nullptr need not branch.
class TransientPin {
public:
    TransientPin(proto::ProtoContext* ctx, const proto::ProtoObject* obj)
        : ctx_(ctx), active_(false), slot_(0) {
        if (!ctx_ || !obj) return;
        // The scratch region lives at the top of automaticLocals; it exists
        // only once the array is sized to kEngineSlotCapacity. Engine-driven
        // call sites (runWithArgs / restoreFrames) always size the context
        // first, so this is normally a no-op. A few non-engine call sites
        // (STRuntime::registryAdd / registryRemove / runTopLevel) may pin on
        // a worker / root context an engine has not yet sized — size it here
        // so TransientPin is self-sufficient on ANY context. resizeAutomatic-
        // Locals is grow-only and idempotent; the array is reallocated at
        // most once per context (~64 KB), well before GC pressure builds.
        if (ctx_->getAutomaticLocalsCount() < kEngineSlotCapacity) {
            ctx_->resizeAutomaticLocals(kEngineSlotCapacity);
        }
        // Scratch overflow: more than kScratchSlots live pins. A hard error,
        // never silent slot reuse.
        if (g_scratchCursor >= kScratchSlots) {
            assert(false && "TransientPin: scratch slot region exhausted");
            return;
        }
        slot_   = kEngineSlotCapacity - 1 - g_scratchCursor;
        ++g_scratchCursor;
        active_ = true;
        ctx_->setAutomaticLocal(slot_, obj);
    }

    ~TransientPin() {
        if (!active_) return;
        // LIFO discipline: the slot we release must be the most recently
        // claimed one. C++ scope nesting guarantees this for stack-allocated
        // pins; the assert catches a misuse (e.g. a heap-allocated pin
        // outliving a later one).
        assert(g_scratchCursor > 0 &&
               slot_ == kEngineSlotCapacity - g_scratchCursor &&
               "TransientPin: non-LIFO release");
        --g_scratchCursor;
        // Clear the slot so a stale pointer is not retained past the pin's
        // lifetime (keeps the GC from over-retaining a now-dead transient).
        ctx_->setAutomaticLocal(slot_, PROTO_NONE);
    }

    // Re-point the pin at a different object. Used when a transient is
    // rebuilt in place inside a loop (e.g. an accumulator list reassigned by
    // appendLast each iteration): the SAME pin slot tracks the latest value
    // without claiming a fresh slot per iteration.
    void reset(const proto::ProtoObject* obj) {
        if (!active_ || !ctx_) return;
        ctx_->setAutomaticLocal(slot_, obj ? obj : PROTO_NONE);
    }

    TransientPin(const TransientPin&)            = delete;
    TransientPin& operator=(const TransientPin&) = delete;
    TransientPin(TransientPin&&)                 = delete;
    TransientPin& operator=(TransientPin&&)      = delete;

private:
    proto::ProtoContext* ctx_;
    bool                 active_;
    unsigned int         slot_;
};

} // namespace protoST

#endif // PROTOST_TRANSIENT_PIN_H
