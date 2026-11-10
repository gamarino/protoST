#pragma once
// The three brackets every primitive that calls protoIO runs its call in,
// shared by io_prims.cpp and binfile_prims.cpp
// (docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md, section 3).
//
// Each runs `f`, which calls protoIO with C++ values only and touches no
// protoCore object, and answers its result. A protoio::Error is caught after
// the scopes have been left, so the protoST error is raised with the context
// managed again (raiseProtoioError, the one shared mapping).

#include "protoST/STRuntime.h"
#include "primitives/ProtoioErrors.h"
#include "protoCore.h"

#include <protoio/error.h>

namespace protoST {

// Accounts a blocking call on a pool worker, so the scheduler can add a
// worker while this one waits (STRuntime::enterBlockingIO). Constructed
// before the UnmanagedScope: adding a worker allocates.
struct BlockingIO {
    STRuntime& rt;
    bool active;
    BlockingIO(STRuntime& r, proto::ProtoContext* ctx) : rt(r), active(r.enterBlockingIO(ctx)) {}
    ~BlockingIO() { if (active) rt.leaveBlockingIO(); }
    BlockingIO(const BlockingIO&) = delete;
    BlockingIO& operator=(const BlockingIO&) = delete;
};

// For a call that can wait without bound: on a peer, a child process, a
// terminal, a pipe, another holder of a file lock, or the storage device.
template <typename F>
auto blocking(STRuntime& rt, proto::ProtoContext* ctx, F&& f) -> decltype(f()) {
    try {
        BlockingIO accounted(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        return f();
    } catch (const protoio::Error& e) {
        raiseProtoioError(e);
    }
}

// For a call that can block only briefly (the local file system, binding a
// socket): out of the collector's quorum, without growing the worker pool.
template <typename F>
auto unmanaged(proto::ProtoContext* ctx, F&& f) -> decltype(f()) {
    try {
        proto::ProtoContext::UnmanagedScope out(ctx);
        return f();
    } catch (const protoio::Error& e) {
        raiseProtoioError(e);
    }
}

// For a call that does not block.
template <typename F>
auto immediate(F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const protoio::Error& e) {
        raiseProtoioError(e);
    }
}

} // namespace protoST
