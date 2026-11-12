#pragma once

#include "protoST/STRuntime.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

namespace protoST {

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

// The first closure over `module` in `rt`: creates its `__bc_ptr__` cell,
// anchors it in the runtime's live registry and publishes it on the module
// (STRuntime.cpp).
const proto::ProtoObject* blockCodeRefSlow(STRuntime& rt, proto::ProtoContext* ctx,
                                           const BytecodeModule& module);

// The `__bc_ptr__` value of a closure over `module` in `rt`. When the runtime
// owns the module (a loaded file, or one handed over with adoptModule), every
// closure over one sub-block shares one cell, created with the first and
// rooted once in the runtime's single pinned structure (the live registry):
// creating a closure allocates no cell for its code, and the anchor lives
// exactly as long as the module. A module the embedder keeps (runTopLevel),
// which may be freed while the runtime lives, gets a cell per closure, rooted
// by the closure, so nothing accumulates in the registry; so does a runtime
// other than the first to create closures over the module.
inline const proto::ProtoObject* blockCodeRefFor(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const BytecodeModule& module) {
    if (const proto::ProtoObject* cell = module.codeRefFor(rt.serial())) return cell;
    if (!module.runtimeOwned()) return blockCodeRef(ctx, &module);
    return blockCodeRefSlow(rt, ctx, module);
}

// The module a `__bc_ptr__` value names, or nullptr when `ref` is absent or is
// not an external pointer (an object without code, or a forged value).
inline const BytecodeModule* blockCodeOf(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* ref) {
    const proto::ProtoExternalPointer* ep = ref ? ref->asExternalPointer(ctx) : nullptr;
    return ep ? static_cast<const BytecodeModule*>(ep->getPointer(ctx)) : nullptr;
}

// A per-thread cache of decoded `__bc_ptr__` cells, for the send and block
// paths: decoding an external pointer is two out-of-line protoCore calls
// (asExternalPointer, getPointer), where the integer it replaced was one. It
// holds only cells published on their module (blockCodeRefFor), which the
// runtime's live registry keeps alive for the runtime's lifetime, so a
// cached cell's address cannot be reused while the runtime lives; the
// runtime's serial in the entry keeps a later runtime from matching it.
struct BlockCodeCacheEntry {
    const proto::ProtoObject* cell;
    std::uint64_t             runtimeSerial;
    const BytecodeModule*     module;
};
inline constexpr unsigned kBlockCodeCacheSize = 64;
extern constinit thread_local BlockCodeCacheEntry t_blockCodeCache[kBlockCodeCacheSize];

// blockCodeOf, through the cache.
inline const BytecodeModule* blockCodeOfCached(const STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ref) {
    BlockCodeCacheEntry& e =
        t_blockCodeCache[(reinterpret_cast<std::uintptr_t>(ref) >> 6) & (kBlockCodeCacheSize - 1)];
    if (e.cell == ref && e.runtimeSerial == rt.serial() && ref) return e.module;
    const BytecodeModule* module = blockCodeOf(ctx, ref);
    if (module && module->codeRefFor(rt.serial()) == ref) e = {ref, rt.serial(), module};
    return module;
}

} // namespace protoST
