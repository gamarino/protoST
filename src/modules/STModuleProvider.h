#pragma once
#include "protoCore.h"
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
class STModuleProvider : public proto::ModuleProvider {
public:
    STModuleProvider();
    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                       proto::ProtoContext* ctx) override;
    const std::string& getGUID() const override { return guid_; }
    const std::string& getAlias() const override { return alias_; }

private:
    // Track Y: the cross-runtime path. Runs the load in protoST's own space and
    // returns a namespace object allocated in `callerCtx`, whose values are the
    // protoST objects themselves.
    static const proto::ProtoObject* loadForForeignCaller(
        STRuntime& rt, const std::string& logicalPath,
        proto::ProtoContext* callerCtx);

    std::string guid_;
    std::string alias_;
};

// S6: process-wide association between a ProtoSpace and the STRuntime that
// owns it. The provider resolves the runtime from `ctx->space`, so every
// thread that runs protoST code — the main thread, actor workers, the DAP
// debuggee thread, any thread added later — reaches it without per-thread
// setup. (A thread-local pointer, set only by the constructing thread, made
// `Import from:` answer "module not found" on every other thread.)
//
// STRuntime registers itself in its constructor, before any thread that can
// import is started, and unregisters in its destructor, after those threads
// are joined. The registry lock is held only for the container operation.
void registerSTRuntime(const proto::ProtoSpace* space, STRuntime* rt);
void unregisterSTRuntime(const proto::ProtoSpace* space, STRuntime* rt);
STRuntime* stRuntimeForSpace(const proto::ProtoSpace* space);

// Track Y: the provider's own runtime state, for a caller whose ProtoSpace
// protoST does not own. Returns the single live STRuntime of this process, or
// nullptr when there is none or when there is more than one.
//
// This reads the same registry rather than caching an STRuntime* inside the
// provider, and it does so on purpose: the provider is registered once per
// process under a std::call_once and outlives every STRuntime (protoCore's
// ProviderRegistry never releases it), so a pointer captured at construction
// would dangle as soon as that runtime was destroyed — which a host that
// builds a runtime per test does on every case. The registry already
// register/unregisters in STRuntime's constructor and destructor, so asking it
// gives the same "my own runtime" answer with a lifetime that is correct.
STRuntime* soleSTRuntime();

// Track Y: how many STRuntimes are registered right now. Exposed so a test can
// assert the ambiguous case (two runtimes, a foreign importer) is refused
// rather than silently served by an arbitrary one.
std::size_t registeredSTRuntimeCount();

} // namespace protoST
