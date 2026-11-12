#pragma once
#include <atomic>
#include <cstdint>

#include "protoCore.h"

namespace protoST {

class STRuntime;

// The body of a primitive: the runtime it runs in, the context, the receiver
// and the positional arguments as an array.
using PrimFn = const proto::ProtoObject* (*)(
    STRuntime&,
    proto::ProtoContext*,
    const proto::ProtoObject* receiver,
    const proto::ProtoObject* const* args,
    int argc);

// ---------------------------------------------------------------------------
// The runtime that owns a context's space
//
// A primitive is a protoCore method, so it receives only the context; the
// runtime is the one that owns `ctx->space` (one protoST runtime per space,
// src/modules/STModuleProvider.h). That association is a process-wide
// registry behind a mutex; a primitive call reads it through a one-entry
// cache per thread, valid while the registry's generation is unchanged. The
// generation advances on every registration and unregistration, so a cache
// entry never outlives the runtime it names, and a thread needs no setup to
// reach its runtime.
// ---------------------------------------------------------------------------
namespace detail {

struct RuntimeOfSpaceCache {
    const proto::ProtoSpace* space = nullptr;
    STRuntime* runtime = nullptr;
    std::uint64_t generation = 0;   // 0 never matches: the registry starts at 1
};

// constinit: the cache needs no dynamic initialisation, so reading it is a
// plain thread-local load with no initialisation guard.
extern constinit thread_local RuntimeOfSpaceCache t_runtimeOfSpace;
extern std::atomic<std::uint64_t> g_runtimeRegistryGeneration;

// The registry lookup behind runtimeOf: refills the cache. Throws when no
// protoST runtime owns the context's space.
STRuntime& runtimeOfSlow(proto::ProtoContext* ctx);

// Refuses the named arguments of a call to a primitive.
[[noreturn]] void refuseNamedArguments(proto::ProtoContext* ctx);

// The method cell protoST's own dispatch is calling on this thread
// (callMethodCell), consumed by the primitive it names on entry.
extern constinit thread_local proto::ProtoMethod t_dispatchedMethod;

// Refuses a call to a primitive that did not come from protoST's dispatch.
[[noreturn]] void refuseDirectCall(proto::ProtoContext* ctx);

} // namespace detail

// Calls a method cell that a protoST send found -- a primitive, or a method
// another runtime exports -- with the receiver as `self`. Every send path of
// protoST calls method cells through here, and only through here.
inline const proto::ProtoObject* callMethodCell(proto::ProtoMethod method,
                                                proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ProtoArguments* positional,
                                                const proto::ProtoKeywordArguments* keywords) {
    detail::t_dispatchedMethod = method;
    return method(ctx, self, nullptr, positional, keywords);
}

// The protoST runtime that owns `ctx`'s space. Throws a NativeError when the
// space belongs to no protoST runtime (a caller from another runtime reaches
// protoST through its foreign-send entry, src/modules/STForeignSend.h).
inline STRuntime& runtimeOf(proto::ProtoContext* ctx) {
    const detail::RuntimeOfSpaceCache& c = detail::t_runtimeOfSpace;
    if (c.space == ctx->space &&
        c.generation == detail::g_runtimeRegistryGeneration.load(std::memory_order_acquire))
        return *c.runtime;
    return detail::runtimeOfSlow(ctx);
}

// A primitive as a protoCore method (proto::ProtoMethod): bound on a
// prototype under its selector, it is found by ordinary attribute lookup and
// overridden by ordinary attribute shadowing. A protoST send calls it through
// callMethodCell. The receiver arrives as `self`, the positional arguments as
// the caller's array, unchanged. A primitive takes no named arguments.
//
// It runs only for protoST's own dispatch. A caller outside it -- another
// runtime, in a space of its own or sharing protoST's, or embedder C++ calling
// the cell through protoCore's `call` -- is refused, uniformly: protoST is
// entered from outside through `__st_send__` (docs/INTEROP.md section 4.3),
// whose boundary (its own context, the handlers of protoST code above it
// hidden, errors leaving as ProtoException) a bare primitive cannot provide,
// and which reaches every method, not only the primitives. The test is the
// token callMethodCell leaves for this very method, consumed here so that a
// nested call from another runtime finds none.
template <PrimFn F>
const proto::ProtoObject* primitiveMethod(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* self,
                                          const proto::ParentLink*,
                                          const proto::ProtoArguments* positional,
                                          const proto::ProtoKeywordArguments* keywords) {
    if (detail::t_dispatchedMethod != &primitiveMethod<F>) detail::refuseDirectCall(ctx);
    detail::t_dispatchedMethod = nullptr;
    STRuntime& rt = runtimeOf(ctx);
    if (keywords && keywords->getSize(ctx) != 0) detail::refuseNamedArguments(ctx);
    return F(rt, ctx, self ? self : PROTO_NONE,
             positional ? positional->values() : nullptr,
             positional ? static_cast<int>(positional->count()) : 0);
}

// Binds `method` (a primitiveMethod<F>, or any protoCore method) on a
// prototype under `selector`: a native method cell with no bound receiver
// (ProtoContext::fromMethod(nullptr, ...)).
void bindPrimitive(STRuntime& rt,
                   const proto::ProtoObject* proto,
                   const char* selector,
                   proto::ProtoMethod method);

// Bind a class-side primitive of a built-in class (a constructor such as
// `Array new:`, a constant such as `Float pi`) on the class's metaclass
// (K14, D52, src/runtime/Bootstrap.h): the class understands it, its
// instances do not.
void bindClassPrimitive(STRuntime& rt,
                        const proto::ProtoObject* cls,
                        const char* selector,
                        proto::ProtoMethod method);

} // namespace protoST
