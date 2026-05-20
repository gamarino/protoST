#pragma once

#include <cstddef>
#include <vector>

namespace proto {
    class ProtoContext;
    class ProtoObject;
}

namespace protoST {

class STRuntime;
class BytecodeModule;
struct DebugFrame;

// F6 v3 A: ExecutionEngine is now non-recursive. Each user-method SEND no
// longer creates a sub-engine on the C++ stack — instead a Frame is pushed
// onto the engine's `frames_` vector and the single dispatch loop
// (runLoop) picks it up on the next iteration. This is the foundation for
// the cooperative yield work in F6 v3 C+: a future-yield can later snapshot
// `frames_` and resume it, which is impossible with the previous recursive
// design where every active Smalltalk method occupies an unreachable C++
// stack frame.
//
// Out of scope for F6 v3 A: block invocations from primitive code
// (bool>>ifTrue:, future>>thenDo:, block>>whileTrue:, the value/value:/...
// family). Those primitives still call invokeBlock() which creates a fresh
// ExecutionEngine on the C++ stack. Bounded by primitive nesting depth and
// unrelated to the unbounded user-method recursion this task targets.
class ExecutionEngine {
public:
    explicit ExecutionEngine(STRuntime& rt) : rt_(rt) {}

    // Runs `m` in `ctx`; returns the value at RETURN_TOP (or method RETURN).
    const proto::ProtoObject* run(proto::ProtoContext* ctx,
                                  const BytecodeModule& m,
                                  const proto::ProtoObject* self = nullptr);

    // Runs `m` with `argc` arguments pre-loaded into locals 0..argc-1.
    // Used by BlockClosure>>value etc. (Task 44).
    //
    // `capturedDict` (F3) — optional mutable ProtoObject acting as the closure
    // environment for captured (free) variables in `m`. PUSH_CAPTURED reads
    // attributes from it, STORE_CAPTURED writes them. Pass nullptr if `m`
    // does not use any captured names.
    const proto::ProtoObject* runWithArgs(proto::ProtoContext* ctx,
                                          const BytecodeModule& m,
                                          const proto::ProtoObject* self,
                                          const proto::ProtoObject* const* args,
                                          int argc,
                                          const proto::ProtoObject* capturedDict = nullptr);

    // F6 v3 B: snapshot/restore round-trip for the engine's frame stack.
    //
    // snapshotFrames serialises the entire `frames_` vector (oldest first,
    // most recent last) into a single ProtoObject (a ProtoList of per-frame
    // mutable objects). The encoding captures pc, operand stack, locals,
    // selfObj, capturedDict and an opaque pointer to the frame's
    // BytecodeModule. Modules are NOT cloned — the BytecodeModule* is wrapped
    // as a finalizer-free ExternalPointer because module lifetime is owned
    // by STRuntime's loadedModules vector (or the top-level caller's stack
    // BytecodeModule); the snapshot only borrows the pointer.
    //
    // restoreFrames is the inverse: it clears frames_ and rebuilds it from
    // the snapshot. After restoreFrames returns, the engine is ready to
    // continue execution from where the snapshot was taken — runLoop() will
    // pick up frames_.back() and dispatch the instruction at its pc.
    //
    // These methods are the plumbing for cooperative yield/resume in F6 v3
    // C+: a Future>>wait that cannot resolve synchronously will snapshot
    // the engine, hand the snapshot to the Future as a continuation, and
    // unwind; the worker thread that eventually resolves the Future will
    // restore the snapshot into a fresh engine and resume execution.
    //
    // No behaviour change for normal execution — these methods are not
    // wired into any opcode in F6 v3 B; they exist only to be exercised by
    // the F6 v3 C+ yield path and by the round-trip test in this task.
    const proto::ProtoObject* snapshotFrames(proto::ProtoContext* ctx) const;
    void restoreFrames(proto::ProtoContext* ctx, const proto::ProtoObject* snapshot);

    // F6 v3 C+D: resume helpers used by STRuntime::drainOne on the
    // cooperative-resume path.
    //
    // resumeWith injects the result of the Future>>wait that originally
    // yielded into the resumed engine. When the awaited future resolved
    // (`error` is nullptr), it pushes `value` onto the top frame's
    // operand stack — exactly the slot the original `wait` would have
    // produced on return. When the awaited future rejected (`error` is
    // non-null), it instead throws std::runtime_error carrying the error
    // message; the engine's normal SEND-time exception path will
    // propagate that out through continueRun, where drainOne can reject
    // the message's own future.
    //
    // continueRun re-enters the same dispatch loop runWithArgs uses,
    // operating on the frames_ already restored by restoreFrames +
    // primed by resumeWith. It returns whatever the resumed frame stack
    // ultimately produces at RETURN_TOP.
    void resumeWith(proto::ProtoContext* ctx,
                    const proto::ProtoObject* value,
                    const proto::ProtoObject* error);
    const proto::ProtoObject* continueRun(proto::ProtoContext* ctx);

private:
    // F6 v3 E3: GC-correctness fix. The frame stack no longer holds any
    // ProtoObject* in plain C++ std::vectors — protoCore's tracing GC cannot
    // see the C++ heap. Instead, EVERY ProtoObject* a frame needs (locals,
    // operand stack, self, captured dict) lives in a slot of the ONE engine
    // context's `automaticLocals` array, which the GC already traces.
    //
    // `automaticLocals` is treated as concatenated per-frame regions:
    //
    //   [frame0 region][frame1 region][frame2 region] ...
    //
    // Each frame's region is laid out as:
    //
    //   [ capturedDict ][ self ][ localCount locals ][ maxStack opStack ]
    //   ^baseSlot       ^+1     ^+kHeaderSlots        ^+kHeaderSlots+localCount
    //
    // Frame carries only plain integers/pointers — fine as C++ fields, the GC
    // never needs to see them.
    struct Frame {
        const BytecodeModule* m          = nullptr;
        std::size_t           pc         = 0;
        unsigned int          baseSlot   = 0;   // region start in automaticLocals
        unsigned int          localCount = 0;   // local-slot count for this frame
        unsigned int          maxStack   = 0;   // operand-stack capacity
        unsigned int          sp         = 0;   // current operand-stack depth
    };

    // Header slots reserved at the start of every frame region.
    //   slot 0 = capturedDict, slot 1 = self
    static constexpr unsigned int kHeaderSlots = 2;

    STRuntime&            rt_;
    std::vector<Frame>    frames_;
    proto::ProtoContext*  ctx_ = nullptr;   // the engine context (GC-traced)

    // F6 v3 E3: the automaticLocals slot index where THIS engine's frame
    // regions begin. Captured from the shared thread-local slot cursor at
    // engine entry; nested engines (invoked by primitives) pack their regions
    // above this engine's. Used to rewind the cursor when frames_ is cleared
    // (yield / completion).
    unsigned int          slotBase_ = 0;

    // Total automaticLocals capacity reserved once at engine entry. Frame
    // regions are packed into [0, kSlotCapacity). Overflow is a hard error.
    static constexpr unsigned int kSlotCapacity = 8192;

    // --- per-frame region geometry -----------------------------------------
    static unsigned int frameRegionSize(const Frame& f) {
        return kHeaderSlots + f.localCount + f.maxStack;
    }
    // Slot index where this frame's locals begin.
    static unsigned int localsBase(const Frame& f) {
        return f.baseSlot + kHeaderSlots;
    }
    // Slot index where this frame's operand stack begins.
    static unsigned int opStackBase(const Frame& f) {
        return f.baseSlot + kHeaderSlots + f.localCount;
    }

    // --- slot accessors (route through the GC-traced context) --------------
    const proto::ProtoObject* getLocal(const Frame& f, unsigned int i) const;
    void setLocal(const Frame& f, unsigned int i, const proto::ProtoObject* v);
    const proto::ProtoObject* getSelf(const Frame& f) const;
    void setSelf(const Frame& f, const proto::ProtoObject* v);
    const proto::ProtoObject* getCaptured(const Frame& f) const;
    void setCaptured(const Frame& f, const proto::ProtoObject* v);
    void push(Frame& f, const proto::ProtoObject* v);
    const proto::ProtoObject* pop(Frame& f);
    const proto::ProtoObject* peek(const Frame& f) const;
    const proto::ProtoObject* opAt(const Frame& f, unsigned int depth) const;

    // Push a new frame for module `m`, returning a reference is unsafe across
    // the vector growth; callers re-acquire frames_.back(). Initialises the
    // region's slots to PROTO_NONE, then binds the supplied arg objects into
    // locals 0..argc-1. `self` / `captured` go into the header slots.
    void pushFrame(const BytecodeModule* m,
                   const proto::ProtoObject* self,
                   const proto::ProtoObject* captured,
                   const proto::ProtoObject* const* args,
                   unsigned int argc);

    // Pop the top frame, rewinding the shared thread-local slot cursor.
    void popFrame();

    // Build a DebugFrame (module/pc/stack/locals) from a frame's slot region.
    DebugFrame makeDebugFrame(const Frame& f) const;

    // Single dispatch loop operating on frames_.back(). Returns when frames_
    // becomes empty (the original C++ caller's frame's RETURN_TOP popped the
    // last frame) — the returned value is whatever that final frame produced.
    const proto::ProtoObject* runLoop(proto::ProtoContext* ctx);
};

} // namespace protoST
