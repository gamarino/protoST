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
// protoST already has a fully GC-traced flat slot array: the engine
// context's `automaticLocals`. Frame regions are packed bottom-up from a
// thread-local cursor (`g_slotCursor`).
//
// E5 reserves a FIXED scratch region at the BOTTOM of that same array —
// `[0, kScratchSlots)` — exclusively for transient pins, filled from slot
// kScratchSlots - 1 downward by a separate thread-local cursor. Frame regions
// start above it, at kFrameRegionBase. Because the scratch region is part of
// `automaticLocals`, the GC traces it for free: no ProtoRootSet handle, no
// liveRegistry churn.
//
// Sized on demand (0.6.2). protoCore's stop-the-world root scan visits every
// slot of every thread's automaticLocals, used or not. Up to 0.6.1 each
// engine context was pre-sized to the maximum, kEngineSlotCapacity (1 M
// slots, 8 MB), with the scratch region at its top, so every pause scanned
// 1 M slots per thread that had ever run code: 25-31 ms per pause with
// twelve workers. The array now starts at kInitialEngineSlots and doubles
// only when a frame region needs more (ensureEngineSlots), up to the same
// maximum, so the recursion depth limit is unchanged.
//
// Growing reallocates the array (resizeAutomaticLocals copies the old slots
// and frees the old buffer). That is safe because only the owning thread
// grows its own context, and only while it is running: the collector reads a
// context's array only during a stop-the-world phase, when the owner is
// parked, and never keeps a pointer into it afterwards (the roots it found
// are copied into the collector's own work list). No code keeps a raw pointer
// into the array across a call that can grow it: pushFrame re-reads it after
// growing, and every other access goes through get/setAutomaticLocal. The
// array never shrinks, and slots above the frame cursor are never read by
// the engine, so a stale pointer left there can only over-retain an object
// until the slot is reused, as before.
//
// Why a primitive can pin with only `ctx`
// ---------------------------------------
// A primitive receives `STRuntime& rt, ProtoContext* ctx, ...`. The `ctx` IS
// the engine context that invoked it. The scratch region lives at a FIXED,
// absolute slot range in any engine context, so a primitive pins into the
// exact same range using just `ctx`. The scratch cursor is thread-local and
// grows LIFO across nested engines / primitives on the same C++ stack.
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
//
// Unbounded nesting (K15, 0.10.0)
// ------------------------------
// The scratch region holds a fixed number of pins, but nothing bounds how
// many pins are live at once: every `collect:` / `select:` / `inject:into:`
// keeps its accumulator pinned while its block runs, the block can recurse
// into the same primitive, and the engine nesting limit (kMaxNestedEngines)
// is 1000 levels, not 256 pins; a primitive that recurses in C++ is bounded by
// nothing at all. Until 0.10.0 the 257th live pin hit `assert(false)` and, in a
// release build, returned WITHOUT pinning: the object stayed in a C++ local
// only, the next safepoint submitted its context's young chain, a collection
// freed it, and the primitive then used a recycled cell (SIGSEGV in
// ProtoList::appendLast, or "stack smashing detected" when the recycled cell's
// size overflowed appendLast's on-stack buffer). protoBaseST's codec hit it
// walking values nested 400-509 deep.
//
// A pin that finds the region full now goes on the context's OVERFLOW CHAIN:
// one ProtoList cell `[object, previous head]` per pin, the head kept in the
// reserved scratch slot kOverflowHeadSlot. The chain is one traced root (no
// ProtoRootSet handle per pin) and the number of live pins is bounded only by
// memory. The chain code is out of line (TransientPin.cpp), so a pin in the
// region costs what it did before. Releasing a chain pin restores the head it
// replaced and allocates nothing, so a destructor never reaches a GC point.
// Creating one allocates a single cell; the object being pinned sits in the
// reserved slot kOverflowStageSlot during that allocation, so it is rooted
// throughout. `reset` on a chain pin rebuilds the cells of the pins above it
// on the same context, in one pass (none, for the usual reset of the innermost
// pin). A chain pin released out of LIFO order would un-root every pin above
// it, so that misuse aborts the process in every build, with a message.
#ifndef PROTOST_TRANSIENT_PIN_H
#define PROTOST_TRANSIENT_PIN_H

#include "protoCore.h"

#include <cassert>
#include <cstddef>

namespace protoST {

// Slot geometry of an engine context's automaticLocals:
//   [0, kScratchSlots)                          transient pins (top-down);
//                                               slots 0 and 1 are reserved
//                                               for the overflow chain
//   [kFrameRegionBase, kFrameRegionLimit)       frame regions (bottom-up)
// kEngineSlotCapacity (1,048,576 since 0.4.0: 10,000-deep recursion needs
// ~560,000 frame slots) is the most the array grows to; it starts at
// kInitialEngineSlots.
inline constexpr unsigned int kEngineSlotCapacity = 1u << 20;
inline constexpr unsigned int kScratchSlots       = 256;
inline constexpr unsigned int kFrameRegionBase    = kScratchSlots;
inline constexpr unsigned int kInitialEngineSlots = 4096;   // 32 KB

// The overflow chain's head (PROTO_NONE when empty), and the slot that roots
// the object a chain pin is being created for while its cell is allocated.
inline constexpr unsigned int kOverflowHeadSlot   = 0;
inline constexpr unsigned int kOverflowStageSlot  = 1;
// Pins the region itself holds: slots [kOverflowStageSlot + 1, kScratchSlots).
inline constexpr unsigned int kScratchPinSlots    = kScratchSlots - 2;

// Thread-local scratch cursor. Counts slots CONSUMED in the scratch region;
// the next free scratch slot is `kScratchSlots - 1 - g_scratchCursor`, i.e.
// the region fills from its top downward. Thread-local because a
// ProtoContext is per-thread and several engines / primitives nest LIFO on
// the same OS thread. It never exceeds kScratchPinSlots.
//
// Defined in ExecutionEngine.cpp alongside the frame-region cursor.
extern thread_local unsigned int g_scratchCursor;

// Number of live overflow-chain pins on the calling thread (TransientPin.cpp).
// For tests and diagnostics.
std::size_t overflowPinsOnThisThread();

// Highest value g_slotCursor (the frame-region cursor, defined in
// ExecutionEngine.cpp) is allowed to reach.
inline constexpr unsigned int kFrameRegionLimit = kEngineSlotCapacity;

// Grows `ctx`'s automaticLocals so that slots [0, needed) exist: at least
// kInitialEngineSlots, doubling, never beyond kEngineSlotCapacity. Must be
// called by the thread that owns `ctx`, outside an unmanaged region (see the
// header comment). `needed` must not exceed kEngineSlotCapacity.
inline void ensureEngineSlots(proto::ProtoContext* ctx, unsigned int needed) {
    const unsigned int have = ctx->getAutomaticLocalsCount();
    if (have >= needed) return;
    unsigned int grow = have < kInitialEngineSlots ? kInitialEngineSlots : have;
    while (grow < needed && grow < kEngineSlotCapacity) grow *= 2;
    if (grow > kEngineSlotCapacity) grow = kEngineSlotCapacity;
    ctx->resizeAutomaticLocals(grow);
}

// RAII pin for a `ProtoObject*` held in a C++ local across a possible-GC
// operation. Construction writes the pointer into the engine context's
// GC-traced automaticLocals (a scratch slot, or the overflow chain once the
// region is full), so the tracing collector keeps the object alive.
// Destruction releases it.
//
// `ctx` must be the engine context (the thread's own context). A nullptr
// `ctx` makes the pin an inert no-op. A nullptr `obj` still claims a place
// (holding PROTO_NONE), so a later `reset` to a real object pins it.
class TransientPin {
public:
    TransientPin(proto::ProtoContext* ctx, const proto::ProtoObject* obj)
        : ctx_(ctx), slot_(0) {
        if (!ctx_) return;
        // The scratch region is the bottom of automaticLocals. A few
        // non-engine call sites (STRuntime::registryAdd / registryRemove /
        // runTopLevel) may pin on a worker / root context no engine has
        // sized yet, so make sure it exists.
        ensureEngineSlots(ctx_, kScratchSlots);
        if (g_scratchCursor < kScratchPinSlots) {
            slot_ = kScratchSlots - 1 - g_scratchCursor;
            ++g_scratchCursor;
            const bool stored = ctx_->setAutomaticLocal(slot_, obj ? obj : PROTO_NONE);
            assert(stored && "TransientPin: scratch slot out of range");
            (void)stored;
            return;
        }
        slot_ = pushChain(ctx_, obj ? obj : PROTO_NONE);
    }

    ~TransientPin() {
        if (!ctx_) return;
        if (slot_ >= kScratchSlots) { popChain(ctx_, slot_); return; }
        // LIFO discipline: the slot we release must be the most recently
        // claimed one. C++ scope nesting guarantees this for stack-allocated
        // pins; the assert catches a misuse (e.g. a heap-allocated pin
        // outliving a later one).
        assert(g_scratchCursor > 0 &&
               slot_ == kScratchSlots - g_scratchCursor &&
               "TransientPin: non-LIFO release");
        --g_scratchCursor;
        // Clear the slot so a stale pointer is not retained past the pin's
        // lifetime (keeps the GC from over-retaining a now-dead transient).
        ctx_->setAutomaticLocal(slot_, PROTO_NONE);
    }

    // Re-point the pin at a different object. Used when a transient is
    // rebuilt in place inside a loop (e.g. an accumulator list reassigned by
    // appendLast each iteration): the SAME pin tracks the latest value
    // without claiming a fresh place per iteration.
    void reset(const proto::ProtoObject* obj) {
        if (!ctx_) return;
        if (!obj) obj = PROTO_NONE;
        if (slot_ >= kScratchSlots) { resetChain(ctx_, slot_, obj); return; }
        ctx_->setAutomaticLocal(slot_, obj);
    }

    // The object this pin holds (PROTO_NONE for an empty pin, nullptr for an
    // inert one). For tests and diagnostics.
    const proto::ProtoObject* pinned() const {
        if (!ctx_) return nullptr;
        if (slot_ >= kScratchSlots) return chainPinned(ctx_, slot_);
        return ctx_->getAutomaticLocal(slot_);
    }

    TransientPin(const TransientPin&)            = delete;
    TransientPin& operator=(const TransientPin&) = delete;
    TransientPin(TransientPin&&)                 = delete;
    TransientPin& operator=(TransientPin&&)      = delete;

private:
    // The overflow chain (TransientPin.cpp), kept out of line and cold so the
    // region path above is all that is inlined at a pin site. A chain pin's
    // slot_ is kScratchSlots + its index on the thread's chain-pin stack.
    static unsigned int pushChain(proto::ProtoContext* ctx, const proto::ProtoObject* obj);
    static void popChain(proto::ProtoContext* ctx, unsigned int slot);
    static void resetChain(proto::ProtoContext* ctx, unsigned int slot,
                           const proto::ProtoObject* obj);
    static const proto::ProtoObject* chainPinned(proto::ProtoContext* ctx, unsigned int slot);

    proto::ProtoContext* ctx_;
    unsigned int         slot_;   // region slot, or kScratchSlots + chain index
};

} // namespace protoST

#endif // PROTOST_TRANSIENT_PIN_H
