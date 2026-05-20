#pragma once

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
//   2. NonLocalReturn is intentionally NOT a std::exception subclass. The
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
//      "non-local return: home method has already returned".

namespace proto {
    class ProtoObject;
}

namespace protoST {

class NonLocalReturn {
public:
    NonLocalReturn(unsigned long homeFrameId,
                   const proto::ProtoObject* value) noexcept
        : homeFrameId_(homeFrameId), value_(value) {}

    unsigned long homeFrameId() const noexcept { return homeFrameId_; }
    const proto::ProtoObject* value() const noexcept { return value_; }

private:
    unsigned long             homeFrameId_;
    const proto::ProtoObject*  value_;
};

} // namespace protoST
