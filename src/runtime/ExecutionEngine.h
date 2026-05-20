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

private:
    // F6 v3 A: explicit frame stack. One Frame per active Smalltalk method
    // (top-level module also gets a Frame). User-method SENDs push a new
    // Frame; RETURN/RETURN_TOP pops the current Frame and threads the
    // result back onto the caller frame's operand stack.
    //
    // Field layout mirrors the per-call locals that the old recursive engine
    // held as C++ stack variables.
    struct Frame {
        const BytecodeModule*                   m = nullptr;
        std::size_t                             pc = 0;
        std::vector<const proto::ProtoObject*>  opStack;
        std::vector<const proto::ProtoObject*>  locals;
        const proto::ProtoObject*               selfObj = nullptr;
        const proto::ProtoObject*               capturedDict = nullptr;
    };

    STRuntime&         rt_;
    std::vector<Frame> frames_;

    // Single dispatch loop operating on frames_.back(). Returns when frames_
    // becomes empty (the original C++ caller's frame's RETURN_TOP popped the
    // last frame) — the returned value is whatever that final frame produced.
    const proto::ProtoObject* runLoop(proto::ProtoContext* ctx);
};

} // namespace protoST
