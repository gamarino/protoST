#pragma once
#include <cstddef>
#include <memory>
#include <string>

// Forward declarations to avoid dragging protoCore headers into the public include.
namespace proto {
    class ProtoSpace;
    class ProtoContext;
    class ProtoObject;
    class ProtoRootSet;
    class ProtoString;
}

namespace protoST { class DebuggerRuntime; }

namespace protoST {

class BytecodeModule;
class ExecutionEngine;
struct Bootstrap;
struct PrimitiveRegistry;

class STRuntime {
public:
    STRuntime();
    ~STRuntime();
    STRuntime(const STRuntime&) = delete;
    STRuntime& operator=(const STRuntime&) = delete;

    proto::ProtoSpace*   space()         const;
    proto::ProtoContext* rootCtx()       const;
    proto::ProtoRootSet* asyncRootSet()  const;

    // Mutable globals namespace (PUSH_GLOBAL / STORE_GLOBAL). Allocated at
    // STRuntime construction as a mutable child of objectProto, with "Object"
    // pre-registered so that `Object subclass: ...` compiles can resolve it.
    proto::ProtoObject*  globals()       const;

    // Access to the bootstrap prototype set (Object/Number/...).
    const Bootstrap& bootstrap() const;

    // Access to the primitive registry that backs SEND dispatch.
    PrimitiveRegistry& registry();

    // Access to the debugger runtime (attach state + session entry).
    DebuggerRuntime& debugger();

    // Convert a BytecodeModule constant pool entry to a ProtoObject (lazy materialisation).
    const proto::ProtoObject* materialize(const BytecodeModule& m, size_t constIdx) const;

    // Run a module against the runtime; returns the final value (top of stack at RETURN_TOP).
    const proto::ProtoObject* runTopLevel(const BytecodeModule& m);

    // F6 actor scheduler — single-thread MVP.
    // schedule() is idempotent for already-scheduled actors.
    //
    // F6 v3 E2b: `ctx` is the calling thread's context — used to anchor the
    // actor in the live-registry GC root (registryAdd) so it survives GC
    // while it sits in the readyQueue. Each worker / foreground caller passes
    // its own context, so the key-string allocation never races on a shared
    // per-thread allocation cache.
    void schedule(proto::ProtoContext* ctx, const proto::ProtoObject* actor);
    bool drainOne(proto::ProtoContext* ctx);   // returns true if a message was processed
    size_t scheduledCount() const;              // size of ready queue (for testing)

    // F6 v2 T2: entry called by the managed worker ProtoThread spawned in the
    // constructor. Loops on the scheduler cv, calling drainOne() until shutdown
    // is requested by the destructor. Public for the C-style ProtoMethod
    // trampoline; embedders should not invoke this directly.
    void workerLoop(proto::ProtoContext* ctx);

    // F6 v2 T2: wait briefly on schedCv for a state change.
    //
    // Used by Future>>wait so the foreground thread doesn't busy-spin (nor
    // throw a spurious deadlock) when the worker has stolen the only ready
    // actor. drainOne notifies after every iteration (including future
    // resolution), so this returns quickly whenever the worker makes progress.
    // Returns true if notified within the timeout; false on plain timeout.
    bool waitForSchedulerProgress(unsigned millis);

    // F6 v2 T7: how many worker threads were actually spawned by the
    // constructor. Reflects PROTOST_WORKERS / hardware_concurrency selection
    // and is used by tests to skip the wall-clock parallelism proof when
    // running on a single-core CI.
    size_t workerCount() const;

    // F6-A4 helpers
    // Allocates a new pending Future (mutable child of futureProto) with the
    // canonical attribute layout (__state__=0, __value__=nil, __error__=nil).
    const proto::ProtoObject* newFuture(proto::ProtoContext* ctx);
    // Cheap actor detection: an object is treated as an actor iff it carries
    // a non-nil __wrapped__ attribute (set by Object>>asActor).
    bool isActor(proto::ProtoContext* ctx, const proto::ProtoObject* obj) const;

    // F6 v3 C: thread-local "actor currently being processed by drainOne on
    // THIS thread". Future>>wait consults this to decide between blocking on
    // the future's cv (main thread / non-actor context) and throwing
    // FutureYield (inside an actor handler).
    //
    // drainOne is the sole writer: it sets the pointer to the actor before
    // dispatching the user method, clears it after (including on the
    // FutureYield catch path). Workers each have their own slot because the
    // pointer is thread_local, so two workers processing two different
    // actors don't trample each other.
    void setCurrentActor(const proto::ProtoObject* actor);
    const proto::ProtoObject* currentActor() const;

    // F5 module system
    // Resolve a logical module path (e.g. "mylib") to a filesystem path.
    // Search order: cwd, $STPATH (colon-separated), active venv's
    // lib/protoST/modules/. The ".st" suffix is auto-appended if missing.
    // Returns "" if not found.
    std::string findModuleFile(const std::string& logicalPath) const;
    // Read, parse, compile, and execute the module at filePath. After running
    // the module's top-level, wrap the classes it declared (skipping names
    // starting with '_') as attributes of a freshly allocated module object.
    // Throws std::runtime_error on parse or compile errors.
    const proto::ProtoObject* loadModuleFromFile(
        proto::ProtoContext* ctx, const std::string& filePath, const std::string& logicalName);

    // Loads a module by logical path with caching. Throws if not found.
    const proto::ProtoObject* loadModule(proto::ProtoContext* ctx, const std::string& logicalPath);

    // T5-a (cross-language interop, consumer side). Appends a UMD provider
    // spec to this space's module-resolution chain so that `Import from:` can
    // reach modules published by another protoCore runtime (protoJS,
    // protoPython, …) or any other registered `proto::ModuleProvider`.
    //
    // `providerSpec` is a UMD spec string in the form "provider:<alias>" or
    // "provider:<guid>" — the same form protoCore's resolution chain uses; the
    // provider itself must already be registered with
    // `proto::ProviderRegistry::instance()`. Out of the box protoST's chain is
    // just its own `provider:st` plus the filesystem fallback, so without this
    // call a foreign provider is registered-but-unreachable. The foreign spec
    // is appended *after* `provider:st`, so a protoST module of the same
    // logical name still wins; a host embedding protoST alongside another
    // runtime calls this once per foreign provider at startup.
    //
    // Idempotent: a spec already present in the chain is not added twice.
    void addModuleProviderToChain(const std::string& providerSpec);

    inline const char* versionTag() const { return "0.1.0-pre"; }

private:
    // F6 v3 E2b: live-registry GC anchoring. registryAdd makes `o` reachable
    // from the single pinned root (so it survives GC); registryRemove drops
    // it. Both serialize under the scheduler mutex. No-ops for null /
    // PROTO_NONE / before the registry is created.
    void registryAdd(proto::ProtoContext* ctx, const proto::ProtoObject* o);
    void registryRemove(proto::ProtoContext* ctx, const proto::ProtoObject* o);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline const char* versionString() { return "protoST 0.1.0-pre"; }

} // namespace protoST
