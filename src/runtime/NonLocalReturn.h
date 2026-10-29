#pragma once
#include "protoCore.h"  // proto::proto_ulong
#include "runtime/PinnedValue.h"

// Track 1, slice 1: NonLocalReturn — the C++ exception that carries an
// `^expr` out of a block whose home method activation lives in an OUTER
// ExecutionEngine.
//
// Control-flow contract:
//
//   1. `Op::RETURN` executed in a block frame (f.homeFrameId != f.frameId)
//      first searches THIS engine's `frames_` for the home frame
//      (frameId == f.homeFrameId).
//        * Found  → the engine unwinds locally (pops every frame from the
//          top down to and including the home frame) — no exception.
//        * Not found → the home lives in an outer engine reached via
//          `invokeBlock`'s nested-engine boundary. The engine throws
//          NonLocalReturn{ homeFrameId, value }.
//
//   2. NonLocalReturn is intentionally NOT a std::exception subclass, nor a
//      proto::ProtoException (a language value; see PinnedValue.h). The
//      nested-engine call sites (invokeBlock, primitives) and STRuntime
//      paths catch `const std::exception&` for ordinary error handling; a
//      NonLocalReturn must slip past those and bubble to the engine that
//      actually owns the home frame, which catches it explicitly.
//
//   3. ExecutionEngine::runLoop wraps its dispatch so a NonLocalReturn
//      reaching the top of an engine is handled: if this engine's `frames_`
//      contains the home frame it unwinds to it locally and resumes with
//      the value; otherwise it lets the exception propagate to the parent.
//
//   4. If a NonLocalReturn escapes the outermost engine no live frame owns
//      the home (the home method already returned — a "dead home"). The
//      outermost C++ entry points (STRuntime::runTopLevel, STRuntime's
//      actor drain) convert it to a std::runtime_error carrying
//      kBlockCannotReturnText.

namespace protoST {

// The text of BlockCannotReturn. Whether the home is alive is asked of this
// thread's engines only, so a home method still running on another thread (a
// block sent to another actor) cannot be told from one that has returned; the
// text names both cases rather than claim the first.
inline constexpr const char* kBlockCannotReturnText =
    "non-local return: the block's home method is not active on this thread "
    "(it has returned, or the block runs in another actor or thread)";

class NonLocalReturn {
public:
    // The value is pinned while the return is in flight: unwinding to the
    // home frame runs ensure: blocks, which may collect (PinnedValue.h).
    NonLocalReturn(proto::ProtoContext* ctx, proto::proto_ulong homeFrameId,
                   const proto::ProtoObject* value)
        : homeFrameId_(homeFrameId), value_(ctx, value) {}

    proto::proto_ulong homeFrameId() const noexcept { return homeFrameId_; }
    const proto::ProtoObject* value() const noexcept { return value_.get(); }

private:
    proto::proto_ulong homeFrameId_;
    PinnedValue        value_;
};

} // namespace protoST
