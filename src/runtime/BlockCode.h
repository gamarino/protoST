#pragma once

#include "protoCore.h"

namespace protoST {

class BytecodeModule;

// The code of a block or method closure: its `__bc_ptr__` attribute is a
// protoCore external pointer to the BytecodeModule the closure runs (0.10.0;
// it was the module's address as an integer, which any integer under that key
// could forge).
//
// The pointer borrows the module: it has no finalizer, because a module is
// owned by STRuntime::loadedModules (an imported or loaded file, kept for the
// runtime's lifetime) or by the embedder that runs it (runTopLevel), as the
// frame snapshots of a suspended actor borrow it (ExecutionEngine.cpp,
// snapshotFrames). Only an external pointer is read as code: a closure is
// recognised by its tag, never by an integer.

// A `__bc_ptr__` value for `module`. Allocates one cell in `ctx`.
inline const proto::ProtoObject* blockCodeRef(proto::ProtoContext* ctx,
                                              const BytecodeModule* module) {
    return ctx->fromExternalPointer(const_cast<BytecodeModule*>(module), /*finalizer=*/nullptr);
}

// The module a `__bc_ptr__` value names, or nullptr when `ref` is absent or is
// not an external pointer (an object without code, or a forged value).
inline const BytecodeModule* blockCodeOf(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* ref) {
    const proto::ProtoExternalPointer* ep = ref ? ref->asExternalPointer(ctx) : nullptr;
    return ep ? static_cast<const BytecodeModule*>(ep->getPointer(ctx)) : nullptr;
}

} // namespace protoST
