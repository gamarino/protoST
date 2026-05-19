// ActorLock.h — F6 v2 T3: per-actor mutex helper.
//
// Each actor (object wrapped via Object>>asActor) carries an `__lock__`
// attribute whose value is an ExternalPointer to a heap-allocated std::mutex.
// The mutex is installed by prim_Object_asActor at construction time and is
// destroyed by a finalizer (registered with fromExternalPointer) when the GC
// reclaims the actor.
//
// The lock serialises the read-modify-write of `__mailbox__` between:
//   1. the SEND fast-path in ExecutionEngine.cpp (foreground thread appending
//      a new message), and
//   2. STRuntime::drainOne (the worker thread, or a Future>>wait drive from
//      the foreground thread, popping the oldest message).
//
// Both call sites use a std::lock_guard<std::mutex> around the read + cons +
// setAttribute sequence so the resulting mailbox state is always consistent
// even with parallel sends and drains.
//
// Notes:
// * The mutex pointer is intentionally non-null for any object on which
//   asActor was called. Callers should still check getActorLock() against
//   nullptr — a non-actor receiver hits a SEND code path that never reaches
//   the mailbox RMW, so an unconditional fallback is fine.
// * The std::mutex finalizer (`delete static_cast<std::mutex*>(p)`) runs at GC
//   time on whatever thread the collector is running on. By construction no
//   thread can be holding this mutex when the actor becomes unreachable, so
//   deleting an unlocked mutex is safe.

#pragma once

#include "protoCore.h"

#include <mutex>

namespace protoST {

// Look up the per-actor std::mutex installed by Object>>asActor.
// Returns nullptr if `actor` is not an actor or if for any reason the
// `__lock__` attribute is absent. Callers should treat a nullptr result as
// "this object is not an actor; no locking needed" and follow the regular
// dispatch path.
inline std::mutex* getActorLock(proto::ProtoContext* ctx,
                                const proto::ProtoObject* actor) {
    if (!actor || actor == PROTO_NONE) return nullptr;
    static const proto::ProtoString* lockKey =
        proto::ProtoString::createSymbol(ctx, "__lock__");
    auto* lockObj = actor->getAttribute(ctx, lockKey);
    if (!lockObj || lockObj == PROTO_NONE) return nullptr;
    auto* ep = lockObj->asExternalPointer(ctx);
    if (!ep) return nullptr;
    return static_cast<std::mutex*>(ep->getPointer(ctx));
}

} // namespace protoST
