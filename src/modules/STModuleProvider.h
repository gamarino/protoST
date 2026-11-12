#pragma once
#include "protoCore.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace protoST {

class STRuntime;

// F5 v2 / Track Y: ModuleProvider that exposes protoST source modules to
// protoCore's Universal Module Discovery (UMD).
//
// `tryLoad(path, ctx)` receives the CALLER's context, and the caller may be
// another runtime co-resident in this process (protoScala, protoPython, ...).
// Each runtime owns its own ProtoSpace, so there are two cases:
//
//   * `ctx->space` is a space protoST owns — an ordinary protoST import, from
//     any of its threads. Unchanged since S6: the runtime comes from the
//     space-keyed registry below and the load runs on the caller's context.
//
//   * `ctx->space` belongs to ANOTHER runtime — a cross-runtime import. The
//     runtime can no longer come from `ctx->space` (that was the Phase 6
//     defect: the lookup missed and the import was reported as "no module").
//     It comes from the provider's own state instead: `soleSTRuntime()`. The
//     module's top level then runs in protoST's OWN space, because protoST's
//     prototypes, globals and interned symbols live there, and `ctx` is used
//     only to build the result the caller sees (see `tryLoad`).
//
// K1: each STRuntime registers a provider of its own, with a GUID of its own
// (`serial` is the runtime's STRuntime::serial()), because protoCore keys its
// process-wide module cache by provider GUID and logical path and never drops
// an entry: under one process-wide GUID, a runtime constructed after another
// was destroyed found the dead runtime's modules in that cache and ran methods
// whose bytecode had been freed. The runtime's own resolution chain names its
// provider by GUID; the provider has no alias. When the runtime is destroyed
// its provider retires: it answers a GUID no entry was ever stored under and
// loads nothing, so no caller, protoST's or a foreign one, can reach a module
// of a dead runtime. protoCore's ProviderRegistry keeps every provider for the
// life of the process (it has no removal), so a retired provider stays
// registered, inert. Foreign callers name protoST by the alias `st`, which
// belongs to STDispatchProvider below.
class STModuleProvider : public proto::ModuleProvider {
public:
    explicit STModuleProvider(unsigned long long serial);
    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                       proto::ProtoContext* ctx) override;
    const std::string& getGUID() const override {
        return retired_.load(std::memory_order_acquire) ? retiredGuid_ : guid_;
    }
    const std::string& getAlias() const override { return alias_; }
    // The resolution-chain entry that names this provider ("provider:<GUID>").
    std::string chainSpec() const { return "provider:" + guid_; }
    // Called by the runtime's destructor once no thread of the runtime can
    // import any more. Irreversible.
    void retire() { retired_.store(true, std::memory_order_release); }

private:
    // Track Y: the cross-runtime path. Runs the load in protoST's own space and
    // returns a namespace object allocated in `callerCtx`, whose values are the
    // protoST objects themselves.
    static const proto::ProtoObject* loadForForeignCaller(
        STRuntime& rt, const std::string& logicalPath,
        proto::ProtoContext* callerCtx);

    std::string guid_;
    std::string retiredGuid_;
    std::string alias_;
    std::atomic<bool> retired_{false};
};

// `provider:st`: the one provider of the process under the alias `st`, with
// no runtime of its own, registered once. It serves the process's sole live
// runtime: tryLoad delegates to that runtime's provider, and getGUID answers
// that provider's GUID, so the module cache entries made through the alias
// are the runtime's own and die with it (K1). With no live runtime, or with
// two, every call answers a new GUID that no other call ever answers, so
// nothing stored under it can be found again; tryLoad then loads nothing (no
// runtime) or refuses the load as ambiguous (two).
//
// protoCore reads getGUID before it calls tryLoad. A runtime constructed or
// destroyed between the two calls can make them disagree; constructing or
// destroying a runtime while another runtime imports from protoST is a host
// lifetime error (INTEROP.md 4.2).
class STDispatchProvider : public proto::ModuleProvider {
public:
    STDispatchProvider();
    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                       proto::ProtoContext* ctx) override;
    const std::string& getGUID() const override;
    const std::string& getAlias() const override { return alias_; }

private:
    std::string alias_;
};

// S6: process-wide association between a ProtoSpace and the STRuntime that
// owns it. The provider resolves the runtime from `ctx->space`, so every
// thread that runs protoST code — the main thread, actor workers, the DAP
// debuggee thread, any thread added later — reaches it without per-thread
// setup. (A thread-local pointer, set only by the constructing thread, made
// `Import from:` answer "module not found" on every other thread.)
//
// A runtime is registered in two steps, so no thread but the constructing one
// can reach a runtime that is not fully built:
//
//   * reserveSTRuntime, first thing in construction: the space's entry is
//     taken, so a second runtime for a space is refused (std::logic_error)
//     before anything is installed in the space. A space hosts one protoST
//     runtime, since a primitive finds its runtime by its context's space
//     (primitives.h, runtimeOf). The reserved entry is visible only to the
//     thread inside a ConstructingSTRuntimeScope for that runtime, whose
//     kernel loading runs primitives and imports.
//   * publishSTRuntime, once construction is done but for starting the worker
//     pool (the first code on another thread that needs the runtime): the
//     entry gets the runtime's own module provider (K1), which protoCore owns
//     by then, and becomes visible to every thread.
//
// unregisterSTRuntime removes the entry, published or not: in the destructor,
// after the runtime's threads are joined, or when construction fails. Every
// change advances the generation runtimeOf's per-thread cache checks. The
// registry lock is held only for the container operation.
void reserveSTRuntime(const proto::ProtoSpace* space, STRuntime* rt);
void publishSTRuntime(const proto::ProtoSpace* space, STRuntime* rt,
                      STModuleProvider* provider);

// Marks the calling thread as the one constructing `rt` for its lifetime, so
// the registry's lookups on this thread find `rt` while it is only reserved.
class ConstructingSTRuntimeScope {
public:
    explicit ConstructingSTRuntimeScope(const STRuntime* rt);
    ~ConstructingSTRuntimeScope();
    ConstructingSTRuntimeScope(const ConstructingSTRuntimeScope&) = delete;
    ConstructingSTRuntimeScope& operator=(const ConstructingSTRuntimeScope&) = delete;

private:
    const STRuntime* previous_;
};
void unregisterSTRuntime(const proto::ProtoSpace* space, STRuntime* rt);
STRuntime* stRuntimeForSpace(const proto::ProtoSpace* space);

// Track Y: the provider's own runtime state, for a caller whose ProtoSpace
// protoST does not own. Returns the single live STRuntime of this process, or
// nullptr when there is none or when there is more than one.
//
// This reads the same registry rather than caching an STRuntime* inside the
// provider, and it does so on purpose: a provider outlives its STRuntime
// (protoCore's ProviderRegistry never releases it), so a pointer captured at
// construction would dangle once that runtime was destroyed — which a host
// that builds a runtime per test does on every case. The registry already
// register/unregisters in STRuntime's constructor and destructor, so asking it
// gives the same "my own runtime" answer with a lifetime that is correct.
STRuntime* soleSTRuntime();

// Track Y: how many STRuntimes are registered and visible to the caller now. Exposed so a test can
// assert the ambiguous case (two runtimes, a foreign importer) is refused
// rather than silently served by an arbitrary one.
std::size_t registeredSTRuntimeCount();

} // namespace protoST
