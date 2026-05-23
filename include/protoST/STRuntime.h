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

    // F6 v4 (2026-05-23): event-driven main-thread Future-wait primitives.
    //
    // Replaces the previous sleep-poll / spin in `prim_Future_wait`. The
    // non-actor caller of Future>>wait calls (in order):
    //   markMainWaitingOn(future);   // tell settlers which future we wait for
    //   acquireMainWait(ctx);        // park until that future's settler releases
    //   ... loop until state observed non-pending ...
    //   markMainWaitingOn(nullptr);  // before returning
    //
    // The settler of every future calls `notifyMainWaiterIfFor(future)`
    // after publishing the new state; the check is one atomic load, the
    // release at most one futex syscall when the main is actually parked
    // ON THIS future. Unrelated settles incur zero cost — they read the
    // pointer, see it doesn't match, exit.
    void markMainWaitingOn(const proto::ProtoObject* future);
    void acquireMainWait(proto::ProtoContext* ctx);
    void notifyMainWaiterIfFor(const proto::ProtoObject* future);

    // F6 v5 (2026-05-23): per-actor blocking lock. Each actor gets a
    // heap-allocated `ActorLock` (binary_semaphore) attached via the
    // `__lockHandle__` ExternalPointer attribute on construction (asActor).
    // attachActorLock is called by asActor; acquire/release are called by
    // workers in drainOne to enforce single-thread-of-execution per actor.
    // The acquire is GC-safe (bracketed by enter/exitGcBlocking).
    void attachActorLock(proto::ProtoContext* ctx, const proto::ProtoObject* actor);
    void acquireActorLock(proto::ProtoContext* ctx, const proto::ProtoObject* actor);
    void releaseActorLock(proto::ProtoContext* ctx, const proto::ProtoObject* actor);

    // F6 v5 (2026-05-23): publish a task on the global task list. The task
    // is a ProtoObject carrying `__actor__`, selector, args, future, and
    // (optionally) `__resume__` (for resume-from-yield tasks). CAS-appends
    // to `liveRegistry.__tasks__` (a ProtoList, FIFO) and releases the
    // worker semaphore to wake one sleeping worker. O(log N_tasks) per
    // append — typical N is small (≤ N_workers under normal load).
    void enqueueTask(proto::ProtoContext* ctx, const proto::ProtoObject* task);

    // Build and enqueue a resume-task (no selector / args / future-to-settle —
    // just `__actor__` = actor and `__resume__` = TRUE). Pushed by the settler
    // of a future that has one or more parked waiter actors; workers pick it
    // up, acquire the actor lock, and restore the suspended frame.
    void enqueueResumeTask(proto::ProtoContext* ctx, const proto::ProtoObject* actor);

    // F6 v2 T7: how many worker threads were actually spawned by the
    // constructor. Reflects PROTOST_WORKERS / hardware_concurrency selection
    // and is used by tests to skip the wall-clock parallelism proof when
    // running on a single-core CI.
    size_t workerCount() const;

    // F6 v6 (2026-05-23 night): worker-pool gate. Lets a caller pause the
    // entire pool — workers in flight finish their current drainOne, but
    // no NEW drains start while paused. SEND fast-paths keep enqueuing
    // tasks normally and the workerSem still wakes parked workers; the
    // gate gets checked at the TOP of every worker iteration, so a woken
    // worker re-parks on a condition variable until startProcessing
    // flips the flag.
    //
    // Designed for the "load everything then measure pure drain" pattern:
    // a benchmark calls stopProcessing(), enqueues N actors-worth of
    // pre-built mailbox content from the main thread, then calls
    // startProcessing() and times the drain to completion. Without the
    // gate, workers were draining concurrently with the main producer
    // and the measurement mixed producer throughput with consumer
    // throughput.
    void stopProcessing();
    void startProcessing();
    bool isProcessingPaused() const;

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

    inline const char* versionTag() const { return "0.2.0"; }

private:
    // F6 v3 E2b: live-registry GC anchoring. registryAdd makes `o` reachable
    // from the single pinned root (so it survives GC); registryRemove drops
    // it. No-ops for null / PROTO_NONE / before the registry is created.
    void registryAdd(proto::ProtoContext* ctx, const proto::ProtoObject* o);
    void registryRemove(proto::ProtoContext* ctx, const proto::ProtoObject* o);

    // Anchor / unanchor an actor in the live registry — but only a genuinely
    // suspended actor needs it. A scheduled actor is rooted by the `__ready__`
    // ProtoList; a running one by drainOne's TransientPin. So registryAdd /
    // registryRemove (which build a per-pointer hex-string key — measured the
    // #1 message-path cost) run ONLY when an actor parks on a future, gated by
    // a cheap per-actor `__anchored__` flag so the common non-suspending path
    // pays nothing.
    void anchorActor(proto::ProtoContext* ctx, const proto::ProtoObject* actor);
    void unanchorActor(proto::ProtoContext* ctx, const proto::ProtoObject* actor);

    // Lock-free scheduler primitives — there is no scheduler mutex.
    //
    // The ready queue is a protoCore immutable ProtoList held under
    // `liveRegistry.__ready__`, mutated by compare-and-swap (the same pattern
    // as the lock-free actor mailbox). The "is this actor owned by the
    // scheduler" state is a per-actor 3-state flag in the `__sched__`
    // attribute, also CAS'd:
    //   0 = idle (not queued, not running);
    //   1 = active (queued, or running a turn — no pending wakeup);
    //   2 = running a turn AND a wakeup arrived — re-queue at turn end.
    void enqueueReady(proto::ProtoContext* ctx, const proto::ProtoObject* actor);
    const proto::ProtoObject* dequeueReady(proto::ProtoContext* ctx);
    long long schedState(proto::ProtoContext* ctx, const proto::ProtoObject* actor);
    bool casSchedState(proto::ProtoContext* ctx, const proto::ProtoObject* actor,
                       long long from, long long to);
    bool mailboxHasWork(proto::ProtoContext* ctx, const proto::ProtoObject* actor);

    // Turn-end finaliser for drainOne, run by its RAII guard on every exit
    // path. Drives the 3-state `__sched__` flag: s==2 means a wakeup arrived
    // during the turn (CAS 2->1, re-queue); s==1 with leftover mailbox work
    // (completed turn) re-queues; s==1 otherwise releases (CAS 1->0). A
    // `suspended` turn (yielded on an awaited future) never re-queues for
    // mailbox reasons — the future's settle reschedules it — but still
    // consumes a pending wakeup via the 2->1 path. The 3-state flag makes the
    // decision atomic against a concurrent SEND / future-settle with no lock.
    void finishDrain(proto::ProtoContext* ctx, const proto::ProtoObject* actor,
                     bool suspended);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline const char* versionString() { return "protoST 0.2.0"; }

} // namespace protoST
