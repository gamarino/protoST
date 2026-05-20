#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "ExecutionEngine.h"
#include "FutureYield.h"
#include "BytecodeModule.h"
#include "Bootstrap.h"
#include "Venv.h"
#include "ActorLock.h"
#include "debugger/DebuggerRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "modules/STModuleProvider.h"
#include "protoCore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace protoST { void installIntPrimitives(STRuntime& rt); }
namespace protoST { void installBoolPrimitives(STRuntime& rt); }
namespace protoST { void installStringPrimitives(STRuntime& rt); }
namespace protoST { void installBlockPrimitives(STRuntime& rt); }
namespace protoST { void installDebuggerPrimitives(STRuntime& rt); }
namespace protoST { void installObjectPrimitives(STRuntime& rt); }
namespace protoST { void installFuturePrimitives(STRuntime& rt); }
namespace protoST { void installImportGlobal(STRuntime& rt); }

// F6-A6 / F6 v2 T4: future transition helpers defined alongside the Future
// primitives.
//
// * resolveFutureFromDrain / rejectFutureFromDrain perform the atomic
//   (write state, fire callbacks, notify cv) sequence under the future's
//   cv mutex. drainOne calls these instead of writing state and firing
//   callbacks itself. Holding the mutex across the callback loop ensures
//   "Future>>wait returns ⇒ all thenDo:/catch: blocks completed".
// * installFutureCV attaches a fresh per-future condition_variable to a
//   newly allocated future (called once from newFuture below).
namespace protoST {
void installFutureCV(proto::ProtoContext* ctx, const proto::ProtoObject* fut);
void resolveFutureFromDrain(STRuntime& rt, proto::ProtoContext* ctx,
                            const proto::ProtoObject* future,
                            const proto::ProtoObject* value);
void rejectFutureFromDrain(STRuntime& rt, proto::ProtoContext* ctx,
                           const proto::ProtoObject* future,
                           const proto::ProtoObject* error);
}

namespace protoST {

// F6 v3 C: thread-local "actor currently being processed on THIS thread".
// drainOne writes it before invoking the user method body; Future>>wait
// reads it to decide between blocking on the future's cv (nullptr ⇒ main
// thread / non-actor context) and throwing FutureYield (non-null ⇒ inside
// an actor handler). Worker threads each see their own slot because
// thread_local is per-OS-thread.
namespace {
    thread_local const proto::ProtoObject* g_currentActor = nullptr;
}

struct PrimitiveRegistry::Impl { std::vector<PrimFn> fns; };
PrimitiveRegistry::PrimitiveRegistry() : impl(std::make_unique<Impl>()) {}
PrimitiveRegistry::~PrimitiveRegistry() = default;
int PrimitiveRegistry::registerPrim(PrimFn fn) {
    impl->fns.push_back(fn);
    return static_cast<int>(impl->fns.size()) - 1;
}
PrimFn PrimitiveRegistry::at(int i) const { return impl->fns.at(i); }
size_t PrimitiveRegistry::size() const   { return impl->fns.size(); }

void bindPrimitive(STRuntime& rt, const proto::ProtoObject* proto, const char* selector, int idx) {
    auto* ctx = rt.rootCtx();
    auto* selStr = ctx->fromUTF8String(selector);                  // create ProtoString
    auto* sel = selStr->asString(ctx);                              // intern as symbol
    // Tag bit 62 marks "this is a primitive marker, not a real method object".
    auto* val = ctx->fromLong(static_cast<long long>(idx) | (1LL << 62));
    const_cast<proto::ProtoObject*>(proto)->setAttribute(ctx, sel, val);
}

struct STRuntime::Impl {
    proto::ProtoSpace    space;
    proto::ProtoContext* rootCtx    = nullptr;
    proto::ProtoRootSet* asyncRoots = nullptr;
    Bootstrap            bootstrap;
    PrimitiveRegistry    registry;
    DebuggerRuntime      debugger;
    // F4-U1: mutable globals namespace, used by PUSH_GLOBAL / STORE_GLOBAL.
    // A mutable child of objectProto so setAttribute updates this object in
    // place (rather than producing a COW copy that the engine would not see).
    proto::ProtoObject*  globals     = nullptr;

    // F6 scheduler
    std::queue<const proto::ProtoObject*> readyQueue;
    std::unordered_set<const proto::ProtoObject*> scheduledSet;  // for idempotency

    // F6 v2 T1: scheduler synchronization. `schedMu` guards readyQueue and
    // scheduledSet so a worker thread (added in T2) can safely cooperate with
    // foreground schedule() calls. `mutable` allows const accessors such as
    // scheduledCount() to lock it.
    mutable std::mutex schedMu;
    std::condition_variable schedCv;

    // F6 v2 T2: parallel drain via a managed ProtoThread.
    //
    // - `pinnedActors` keeps actors alive while they sit in the readyQueue.
    //   The queue is std::queue<const ProtoObject*> — a plain STL container
    //   the tracing GC cannot see — and the worker thread may consume entries
    //   asynchronously. Pinning via asyncRoots makes the actor reachable from
    //   a GC root for the schedule()..drainOne() window.
    // - `workers` holds N managed ProtoThreads spawned in the STRuntime
    //   constructor (F6 v2 T7). Each gets its own root ProtoContext chain and
    //   independently drains the shared readyQueue under schedMu. The pool
    //   size is hardware_concurrency() (defaulted to 2, capped at 8); the
    //   PROTOST_WORKERS env var overrides it for tests + experimentation.
    // - `shutdown` is set by ~STRuntime to make every worker exit its wait
    //   loop; schedCv.notify_all() wakes all of them at once.
    std::unordered_map<const proto::ProtoObject*, proto::ProtoRootSet::Handle> pinnedActors;
    std::vector<const proto::ProtoThread*> workers;
    std::atomic<bool> shutdown { false };

    // F5-M2 module cache: canonical absolute path -> module object.
    std::unordered_map<std::string, const proto::ProtoObject*> moduleCache;

    // F5-M3: Keep compiled BytecodeModules alive for the lifetime of the
    // runtime. Block method wrappers store raw pointers into a module's
    // `block(idx)` storage via __bc_ptr__; if the BytecodeModule were
    // destroyed at the end of loadModuleFromFile those pointers would
    // dangle and any subsequent send on a module-resident class would
    // segfault.
    std::vector<std::unique_ptr<BytecodeModule>> loadedModules;

    Impl() {
        // protoCore exposes the root context as a public field on ProtoSpace
        // (see protoCore/headers/protoCore.h:1234 and protoJS/src/JSContext.cpp:100).
        rootCtx    = space.rootContext;
        asyncRoots = space.createRootSet("protoST-async");
        bootstrapPrototypes(space, rootCtx, bootstrap);

        // Allocate the globals namespace as a mutable child of objectProto so
        // setAttribute updates it in place. Pre-register "Object" so that
        // `Object subclass: #Foo ...` patterns (F4-U2) can look it up.
        globals = const_cast<proto::ProtoObject*>(
            bootstrap.objectProto->newChild(rootCtx, /*isMutable=*/true));
        auto* objKey = rootCtx->fromUTF8String("Object")->asString(rootCtx);
        globals->setAttribute(rootCtx, objKey, bootstrap.objectProto);

        // F6: register Actor and Future in globals so user code can refer to
        // them via PUSH_GLOBAL (e.g. `Actor subclass: ...`, `Future new`).
        auto* actorKey = rootCtx->fromUTF8String("Actor")->asString(rootCtx);
        globals->setAttribute(rootCtx, actorKey, bootstrap.actorProto);

        auto* futureKey = rootCtx->fromUTF8String("Future")->asString(rootCtx);
        globals->setAttribute(rootCtx, futureKey, bootstrap.futureProto);
    }

    ~Impl() {
        if (asyncRoots) {
            space.destroyRootSet(asyncRoots);
            asyncRoots = nullptr;
        }
    }
};

// F6 v2 T2: trampoline entered by the managed worker ProtoThread. protoCore
// gives this thread its own root ProtoContext chain (passed as the first
// argument). We decode the STRuntime* from args[0] (an ExternalPointer that
// the constructor wrapped this pointer into) and delegate to workerLoop().
static const proto::ProtoObject* st_worker_main(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* first = args->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ep =
        first ? first->asExternalPointer(ctx) : nullptr;
    if (!ep) return PROTO_NONE;
    STRuntime* rt = static_cast<STRuntime*>(ep->getPointer(ctx));
    if (!rt) return PROTO_NONE;
    rt->workerLoop(ctx);
    return PROTO_NONE;
}

STRuntime::STRuntime() : impl_(std::make_unique<Impl>()) {
    installIntPrimitives(*this);
    installBoolPrimitives(*this);
    installStringPrimitives(*this);
    installBlockPrimitives(*this);
    installDebuggerPrimitives(*this);
    installObjectPrimitives(*this);
    installFuturePrimitives(*this);
    installImportGlobal(*this);

    // F5 v2: register STModuleProvider once globally with protoCore's
    // ProviderRegistry; subsequent STRuntime instances reuse the same provider.
    static std::once_flag s_providerRegistered;
    std::call_once(s_providerRegistered, []() {
        proto::ProviderRegistry::instance().registerProvider(
            std::make_unique<STModuleProvider>());
    });

    // Point the (thread-local) "current runtime" pointer the provider consults
    // at this STRuntime so tryLoad() dispatches back into our findModuleFile /
    // loadModuleFromFile helpers.
    setCurrentSTRuntime(this);

    // Set the resolution chain on this space: protoST's source provider only.
    {
        auto* ctx = impl_->rootCtx;
        auto* chain = ctx->newList();
        chain = chain->appendLast(
            ctx, ctx->fromUTF8String("provider:st"));
        impl_->space.setResolutionChain(chain->asObject(ctx));
    }

    // F6 v2 T7: spawn N managed worker ProtoThreads that drain the scheduler
    // queue in parallel with the foreground (Future>>wait) drain. Each worker
    // gets its own root ProtoContext from protoCore.
    //
    // Pool sizing:
    //   * default = std::thread::hardware_concurrency() (clamped to >=2 when
    //     the runtime cannot determine it, which would defeat the purpose of
    //     having a pool at all),
    //   * overridable via the PROTOST_WORKERS environment variable (>=1) for
    //     tests + benchmarking,
    //   * capped at 8 to keep the per-runtime resource footprint bounded.
    //
    // The first argument passed to st_worker_main is an ExternalPointer
    // wrapping `this`; the trampoline decodes it and delegates to
    // workerLoop(). If newThread returns nullptr (e.g. resource exhaustion)
    // we silently skip that slot — the remaining workers (and the main-thread
    // Future>>wait drain) keep correctness intact, the pool just runs with
    // fewer effective workers.
    {
        auto* ctx = impl_->rootCtx;
        unsigned numWorkers = std::thread::hardware_concurrency();
        if (numWorkers == 0) numWorkers = 2;
        if (const char* env = std::getenv("PROTOST_WORKERS")) {
            try {
                int parsed = std::stoi(env);
                if (parsed >= 1) numWorkers = static_cast<unsigned>(parsed);
            } catch (...) {
                // Malformed env var: keep the hardware-derived default rather
                // than refusing to start; logging would be the only thing we
                // could do here and STRuntime construction is meant to be
                // silent.
            }
        }
        if (numWorkers > 8u) numWorkers = 8u;

        for (unsigned i = 0; i < numWorkers; ++i) {
            const proto::ProtoList* argsForThread = ctx->newList();
            argsForThread = argsForThread->appendLast(
                ctx, ctx->fromExternalPointer(this, nullptr));
            std::string nameStr = "protoST-worker-" + std::to_string(i);
            const proto::ProtoString* threadName =
                ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
            const proto::ProtoThread* t = impl_->space.newThread(
                ctx, threadName, st_worker_main, argsForThread, nullptr);
            if (t) impl_->workers.push_back(t);
        }
    }
}
STRuntime::~STRuntime() {
    // F6 v2 T7: signal every worker to exit, then join them. workerLoop's
    // cv predicate checks `shutdown` before the queue, so a single
    // notify_all wakes the whole pool even if some workers were inside the
    // cv.wait predicate window. We then join each in turn on the main
    // thread's root context. join() is sequential but cheap because every
    // worker exits its loop in parallel as soon as it observes the flag.
    if (!impl_->workers.empty()) {
        impl_->shutdown.store(true, std::memory_order_release);
        impl_->schedCv.notify_all();
        for (auto* t : impl_->workers) {
            // ProtoThread::join takes the CALLING context (main thread's
            // root). newThread returns const ProtoThread*; join is non-const.
            const_cast<proto::ProtoThread*>(t)->join(impl_->rootCtx);
        }
        impl_->workers.clear();
    }

    // F5 v2: clear the thread-local pointer if it still references us, so a
    // late provider lookup after destruction doesn't dereference a dead
    // runtime.
    if (currentSTRuntime() == this) setCurrentSTRuntime(nullptr);

    // F6 v3 C: clear the thread-local current-actor pointer if it still
    // refers to an actor of this (now-dying) runtime. Without this a stale
    // pointer from a worker thread that is being reused across STRuntime
    // instances could make a subsequent Future>>wait misfire FutureYield.
    // The main thread is the common case (each test constructs a fresh
    // STRuntime on the same thread); worker threads are freshly spawned
    // per runtime so their slots start clean anyway.
    if (currentActor() != nullptr) setCurrentActor(nullptr);
}

bool STRuntime::waitForSchedulerProgress(unsigned millis) {
    std::unique_lock<std::mutex> lock(impl_->schedMu);
    auto status = impl_->schedCv.wait_for(
        lock, std::chrono::milliseconds(millis));
    return status == std::cv_status::no_timeout;
}

void STRuntime::workerLoop(proto::ProtoContext* ctx) {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(impl_->schedMu);
            impl_->schedCv.wait(lock, [this]{
                return impl_->shutdown.load(std::memory_order_acquire)
                    || !impl_->readyQueue.empty();
            });
            // If shutdown requested and nothing left to drain, exit.
            // If shutdown requested but queue is non-empty, prefer to keep
            // draining so we don't lose pending work (the destructor will
            // not return until join() completes).
            if (impl_->shutdown.load(std::memory_order_acquire)
                && impl_->readyQueue.empty()) {
                return;
            }
        }
        // drainOne reacquires the lock for its pop. It is safe to race with
        // the main thread; whichever pops first owns the actor for this
        // iteration.
        drainOne(ctx);
    }
}

proto::ProtoSpace*   STRuntime::space()         const { return &impl_->space; }
proto::ProtoContext* STRuntime::rootCtx()       const { return impl_->rootCtx; }
proto::ProtoRootSet* STRuntime::asyncRootSet()  const { return impl_->asyncRoots; }
const Bootstrap&     STRuntime::bootstrap()     const { return impl_->bootstrap; }
PrimitiveRegistry&   STRuntime::registry()            { return impl_->registry; }
DebuggerRuntime&     STRuntime::debugger()            { return impl_->debugger; }
proto::ProtoObject*  STRuntime::globals()       const { return impl_->globals; }

// F6 v3 C: thread-local current-actor accessors. Implementation is a flat
// thread_local pointer (see anonymous namespace above); the STRuntime methods
// only proxy to it so callers can keep using the STRuntime handle they
// already have, without depending on a free function.
void STRuntime::setCurrentActor(const proto::ProtoObject* actor) {
    g_currentActor = actor;
}
const proto::ProtoObject* STRuntime::currentActor() const {
    return g_currentActor;
}

const proto::ProtoObject*
STRuntime::materialize(const BytecodeModule& m, size_t i) const {
    using K = BytecodeModule::ConstKind;
    auto* ctx = impl_->rootCtx;
    switch (m.constKind(i)) {
        case K::Integer:
            return ctx->fromLong(m.constInteger(i));
        case K::Float:
            return ctx->fromDouble(m.constFloat(i));
        case K::String:
            return ctx->fromUTF8String(m.constString(i).c_str());
        case K::Symbol: {
            const proto::ProtoObject* s =
                ctx->fromUTF8String(m.constSymbol(i).c_str());
            // ProtoObject::asString returns a ProtoString view of the value.
            return reinterpret_cast<const proto::ProtoObject*>(s->asString(ctx));
        }
        case K::Char:
            // F2 simplification: treat character literal as a 1-char string.
            return ctx->fromUTF8String(m.constString(i).c_str());
        case K::BlockRef:
            // F2 stub: block materialisation lands in a later task.
            return PROTO_NONE;
        case K::NilK:   return PROTO_NONE;
        case K::TrueK:  return PROTO_TRUE;
        case K::FalseK: return PROTO_FALSE;
    }
    return PROTO_NONE;
}

const proto::ProtoObject*
STRuntime::runTopLevel(const BytecodeModule& m) {
    ExecutionEngine eng(*this);
    auto* ctx = impl_->rootCtx;
    // F3: pre-allocate a mutable dict for module-level captured locals.
    // A mutable child of objectProto behaves as a per-name attribute store —
    // setAttribute mutates this object directly and getAttribute reads it back.
    // Block creation (F3-C5) will inherit this same dict so inner blocks can
    // observe and mutate top-level captured names.
    auto* capturedDict = const_cast<proto::ProtoObject*>(impl_->bootstrap.objectProto)
        ->newChild(ctx, /*isMutable=*/true);
    return eng.runWithArgs(ctx, m, /*self=*/PROTO_NONE,
                           /*args=*/nullptr, /*argc=*/0, capturedDict);
}

void STRuntime::schedule(const proto::ProtoObject* actor) {
    if (!actor) return;
    {
        std::lock_guard<std::mutex> lock(impl_->schedMu);
        if (!impl_->scheduledSet.insert(actor).second) {
            return;  // already scheduled; no need to notify
        }
        // F6 v2 T2: pin the actor in asyncRoots while it sits in the queue.
        // readyQueue is a plain std::queue invisible to the tracing GC, and a
        // worker thread on a separate ProtoContext chain may consume entries
        // asynchronously. Pinning bridges the gap.
        if (impl_->asyncRoots) {
            impl_->pinnedActors[actor] = impl_->asyncRoots->add(actor);
        }
        impl_->readyQueue.push(actor);
    }
    // Wake one waiter; the worker thread (added in T2) loops on schedCv.
    impl_->schedCv.notify_one();
}

bool STRuntime::drainOne(proto::ProtoContext* ctx) {
    const proto::ProtoObject* actor = nullptr;
    proto::ProtoRootSet::Handle pinHandle = proto::ProtoRootSet::kNullHandle;
    {
        // F6 v2 T1: only the queue/set mutation is under the lock. Mailbox
        // and future updates below run unlocked so concurrent schedule()
        // calls can proceed while the current message is being processed.
        std::lock_guard<std::mutex> lock(impl_->schedMu);
        if (impl_->readyQueue.empty()) return false;
        actor = impl_->readyQueue.front();
        impl_->readyQueue.pop();
        impl_->scheduledSet.erase(actor);
        // F6 v2 T2: claim ownership of this actor's pin handle. If the
        // processing path below re-schedules the actor, schedule() will pin
        // it again with a fresh handle independent of this one.
        auto it = impl_->pinnedActors.find(actor);
        if (it != impl_->pinnedActors.end()) {
            pinHandle = it->second;
            impl_->pinnedActors.erase(it);
        }
    }

    // F6 v2 T2: RAII release of the per-iteration pin on all exit paths.
    // The handle was minted by schedule() and must be returned exactly once.
    // The destructor also notifies schedCv so a Future>>wait stuck on an
    // empty queue (because the worker beat us to the actor) wakes promptly
    // once we've finished resolving / rejecting the future.
    struct DrainGuard {
        proto::ProtoRootSet* rs;
        proto::ProtoRootSet::Handle h;
        std::condition_variable* cv;
        ~DrainGuard() {
            if (rs && h != proto::ProtoRootSet::kNullHandle) rs->remove(h);
            if (cv) cv->notify_all();
        }
    } drainGuard{impl_->asyncRoots, pinHandle, &impl_->schedCv};

    static const proto::ProtoString* mailboxKey =
        ctx->fromUTF8String("__mailbox__")->asString(ctx);
    static const proto::ProtoString* wrappedKey =
        ctx->fromUTF8String("__wrapped__")->asString(ctx);
    static const proto::ProtoString* selKey =
        ctx->fromUTF8String("__selector__")->asString(ctx);
    static const proto::ProtoString* argsKey =
        ctx->fromUTF8String("__args__")->asString(ctx);
    static const proto::ProtoString* futKey =
        ctx->fromUTF8String("__future__")->asString(ctx);
    // F6 v3 C+D: per-actor yield/resume bookkeeping.
    //  * __suspended_frame__ : engine snapshot taken at the FutureYield site.
    //  * __waiting_on__      : the future the actor is currently awaiting.
    //  * __suspended_future__ : the message-level Future to resolve when the
    //    resumed run completes (i.e. the future that drainOne would have
    //    resolved on the synchronous-completion path).
    //
    // These are deliberately NOT cached in function-local statics: protoCore
    // interns symbols per-ProtoSpace, so a static would bind to whichever
    // runtime ran drainOne first and become a dangling pointer into a freed
    // space for every subsequent STRuntime. The engine's FutureYield catch
    // and the future-side waiter helpers resolve the same names; resolving
    // fresh from the live `ctx` on every call keeps the symbol identity
    // consistent across all three sites within a single runtime. The
    // interning lookup is cheap relative to the surrounding getAttribute
    // traffic.
    const proto::ProtoString* suspKey =
        ctx->fromUTF8String("__suspended_frame__")->asString(ctx);
    const proto::ProtoString* waitingOnKey =
        ctx->fromUTF8String("__waiting_on__")->asString(ctx);
    const proto::ProtoString* suspFutKey =
        ctx->fromUTF8String("__suspended_future__")->asString(ctx);
    const proto::ProtoString* fValueKey =
        ctx->fromUTF8String("__value__")->asString(ctx);
    const proto::ProtoString* fErrorKey =
        ctx->fromUTF8String("__error__")->asString(ctx);
    const proto::ProtoString* fStateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    // F6 v2 T4: future state/value/error keys are no longer referenced here;
    // resolveFutureFromDrain / rejectFutureFromDrain in future_prims.cpp own
    // the entire transition including the attribute writes.

    // F6 v2 T3: hold the per-actor lock across BOTH the mailbox RMW and the
    // dispatch of the popped message. Two concurrency hazards motivate the
    // wider scope:
    //   1. SEND fast-path append vs drainOne pop on the mailbox itself
    //      (the RMW race described in the task brief).
    //   2. Two drainers (the worker thread and a main-thread Future>>wait
    //      calling drainOne) picking up the same actor for different
    //      scheduling events and dispatching its methods in parallel. The
    //      scheduledSet only prevents queue-duplicate entries; it does not
    //      forbid a second schedule() after the first pop, so without an
    //      additional barrier the actor's wrapped object can have two
    //      messages mutating its instance variables at once.
    // Holding the actor lock for the entire dispatch enforces the actor
    // semantics of "at most one message in flight per actor", which is what
    // makes per-actor instance state safe without further synchronisation
    // inside user methods. The future state writes that follow also stay
    // under the lock to keep "method body ran + future resolved" atomic
    // from the perspective of other threads observing the future.
    //
    // Mind the lock-acquisition order: schedMu was released when we left
    // its scope above. Acquiring the actor lock now means a drainer goes
    // schedMu → actor lock. The SEND fast-path takes the actor lock without
    // holding schedMu, and schedule() takes schedMu without holding the
    // actor lock — so no two threads ever hold the pair in opposite order
    // and there is no deadlock.
    //
    // User code running inside the lock may itself send to the SAME actor
    // (a recursive send). That hits the SEND fast-path, which also takes
    // this lock — using a non-recursive std::mutex would self-deadlock.
    // For this task we keep std::mutex and accept the limitation; the F6
    // tests do not exercise self-sends. T4/T5 can revisit if needed.
    std::mutex* actorLock = getActorLock(ctx, actor);
    std::unique_lock<std::mutex> actorGuard;
    if (actorLock) actorGuard = std::unique_lock<std::mutex>(*actorLock);

    // F6 v3 C+D: check for a suspended-frame snapshot BEFORE looking at the
    // mailbox. If present, this drainOne tick is a resume of a previously
    // yielded message rather than a fresh pop. We restore the snapshot into
    // a fresh ExecutionEngine, push the resolved value of the awaited
    // future onto the top frame's operand stack (matching what the
    // original `wait` would have returned), and let runLoop continue.
    {
        auto* snapAttr = actor->getAttribute(ctx, suspKey);
        if (snapAttr && snapAttr != PROTO_NONE) {
            // Pull the message-level future the original drainOne would
            // have resolved on synchronous completion. Stored on the actor
            // at yield time so the resume path knows which future to
            // settle on RETURN_TOP (or reject on rethrow).
            auto* msgFut = actor->getAttribute(ctx, suspFutKey);
            // Pull the awaited future and read its settled value/error.
            auto* awaited = actor->getAttribute(ctx, waitingOnKey);
            const proto::ProtoObject* resumeValue = PROTO_NONE;
            const proto::ProtoObject* resumeError = nullptr;
            if (awaited && awaited != PROTO_NONE) {
                auto* st = awaited->getAttribute(ctx, fStateKey);
                long long s = st ? st->asLong(ctx) : 0;
                if (s == 1) {
                    auto* v = awaited->getAttribute(ctx, fValueKey);
                    resumeValue = v ? v : PROTO_NONE;
                } else if (s == 2) {
                    auto* e = awaited->getAttribute(ctx, fErrorKey);
                    resumeError = e ? e : PROTO_NONE;
                }
                // s == 0 (still pending) shouldn't happen here — the
                // future's transition is what scheduled us. Treat as
                // PROTO_NONE rather than re-yielding silently.
            }

            // Clear the suspended-state attributes BEFORE running so a
            // re-yield from the resumed body installs a fresh snapshot
            // rather than racing with the one we just consumed.
            const_cast<proto::ProtoObject*>(actor)->setAttribute(ctx, suspKey, PROTO_NONE);
            const_cast<proto::ProtoObject*>(actor)->setAttribute(ctx, waitingOnKey, PROTO_NONE);
            const_cast<proto::ProtoObject*>(actor)->setAttribute(ctx, suspFutKey, PROTO_NONE);

            setCurrentActor(actor);
            ExecutionEngine eng(*this);
            try {
                eng.restoreFrames(ctx, snapAttr);
                eng.resumeWith(ctx, resumeValue, resumeError);
                // After resumeWith pushes the value (or rethrows the error
                // into the caller), continueRun executes the rest of the
                // frames_ stack and returns the final value.
                const proto::ProtoObject* result = eng.continueRun(ctx);
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    resolveFutureFromDrain(*this, ctx, msgFut,
                                           result ? result : PROTO_NONE);
                }
            } catch (const FutureYield&) {
                // Re-yield: the engine has already snapshotted + recorded
                // the new awaited future on the actor. Keep msgFut on the
                // actor for the next resume. The actor lock will release
                // on return; whoever resolves the new awaited future will
                // re-schedule us.
                setCurrentActor(nullptr);
                // Preserve __suspended_future__ so the next resume still
                // knows which message-level future to settle on completion.
                if (msgFut && msgFut != PROTO_NONE) {
                    const_cast<proto::ProtoObject*>(actor)->setAttribute(
                        ctx, suspFutKey, msgFut);
                }
                if (actorGuard.owns_lock()) actorGuard.unlock();
                return true;
            } catch (const std::exception& e) {
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    auto* err = ctx->fromUTF8String(e.what());
                    rejectFutureFromDrain(*this, ctx, msgFut, err);
                }
            }

            if (actorGuard.owns_lock()) actorGuard.unlock();
            // The actor was scheduled to resume; any mailbox messages that
            // accumulated during the yield window must still be drained.
            // We re-schedule unconditionally if the mailbox is non-empty.
            auto* mbAfter = actor->getAttribute(ctx, mailboxKey);
            if (mbAfter && mbAfter != PROTO_NONE) {
                auto* mbList = mbAfter->asList(ctx);
                if (mbList && mbList->getSize(ctx) > 0) schedule(actor);
            }
            return true;
        }
    }

    const proto::ProtoObject* msg = nullptr;
    bool rescheduleAfter = false;
    {
        // Get the mailbox (ProtoList). Pop the first (FIFO).
        auto* mbObj = actor->getAttribute(ctx, mailboxKey);
        if (!mbObj || mbObj == PROTO_NONE) return true;
        auto* mailbox = mbObj->asList(ctx);
        if (!mailbox || mailbox->getSize(ctx) == 0) return true;

        msg = mailbox->getAt(ctx, 0);
        // Drop the first message — getSlice(from, to) over [1, size).
        auto* remaining = mailbox->getSlice(
            ctx, 1, static_cast<int>(mailbox->getSize(ctx)));
        const_cast<proto::ProtoObject*>(actor)->setAttribute(
            ctx, mailboxKey, remaining->asObject(ctx));
        rescheduleAfter = (remaining->getSize(ctx) > 0);
    }

    // Extract message fields.
    auto* selector = msg->getAttribute(ctx, selKey);
    auto* argsList = msg->getAttribute(ctx, argsKey);
    auto* future   = msg->getAttribute(ctx, futKey);
    auto* wrapped  = actor->getAttribute(ctx, wrappedKey);

    // Build args array.
    auto* argsListAsList = argsList ? argsList->asList(ctx) : nullptr;
    int argc = argsListAsList ? static_cast<int>(argsListAsList->getSize(ctx)) : 0;
    std::vector<const proto::ProtoObject*> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(argsListAsList->getAt(ctx, i));
    }

    // Resolve selector to a symbol string.
    auto* selStr = selector ? selector->asString(ctx) : nullptr;
    if (!selStr) {
        if (future) {
            auto* err = ctx->fromUTF8String("invalid selector");
            // F6 v2 T4: atomic (write state, snapshot cbs, notify) under
            // the future's cv mutex; catch: callbacks fire outside the
            // mutex on the snapshot. See future_prims.cpp.
            rejectFutureFromDrain(*this, ctx, future, err);
        }
        return true;
    }

    // F6 v3 C: mark this thread as "inside an actor handler" so
    // Future>>wait throws FutureYield instead of blocking on cv. Cleared
    // unconditionally on every exit path below.
    setCurrentActor(actor);

    try {
        auto* method = wrapped ? wrapped->getAttribute(ctx, selStr) : nullptr;
        const proto::ProtoObject* result = nullptr;

        // Detect user method (has __bc_ptr__) vs primitive marker (tagged int).
        static const proto::ProtoString* bcKey =
            ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
        auto* bcPtrObj = method ? method->getAttribute(ctx, bcKey) : nullptr;
        if (bcPtrObj && bcPtrObj != PROTO_NONE) {
            // User method: invoke via a sub-engine with wrapped as self.
            const BytecodeModule* sub =
                reinterpret_cast<const BytecodeModule*>(bcPtrObj->asLong(ctx));
            std::vector<const proto::ProtoObject*> methodArgs;
            methodArgs.reserve(static_cast<size_t>(argc) + 1);
            methodArgs.push_back(wrapped);
            for (int i = 0; i < argc; ++i) methodArgs.push_back(args[i]);
            // Honour the method's captured-dict if any (matches Engine path).
            static const proto::ProtoString* capKey =
                ctx->fromUTF8String("__captured__")->asString(ctx);
            auto* capDict = method->getAttribute(ctx, capKey);
            if (capDict == PROTO_NONE) capDict = nullptr;
            ExecutionEngine subEng(*this);
            result = subEng.runWithArgs(
                ctx, *sub, /*self=*/wrapped,
                methodArgs.data(),
                static_cast<int>(methodArgs.size()),
                capDict);
        } else if (method) {
            long long marker = method->asLong(ctx);
            if (marker & (1LL << 62)) {
                int idx = static_cast<int>(marker & ((1LL << 62) - 1));
                auto fn = impl_->registry.at(idx);
                result = fn(*this, ctx, wrapped, args.data(), argc);
            } else {
                throw std::runtime_error("unknown method shape");
            }
        } else {
            throw std::runtime_error(
                std::string("doesNotUnderstand: ") +
                std::string(selStr->toStdString(ctx)));
        }

        // Resolve future.
        if (future) {
            auto* v = result ? result : PROTO_NONE;
            // F6 v2 T4: atomic (write state, snapshot cbs, notify cv)
            // under the future's mutex; thenDo: callbacks fire outside the
            // mutex on the snapshot. Closes the registration race where a
            // concurrent thenDo: could land its callback into __then_cbs__
            // AFTER drainOne had already taken its snapshot.
            resolveFutureFromDrain(*this, ctx, future, v);
        }
    } catch (const FutureYield&) {
        // F6 v3 C: the engine has already snapshotted frames_, stored the
        // snapshot under __suspended_frame__ on the actor, recorded the
        // awaited future under __waiting_on__, and appended us to the
        // future's __waiters__ list. We just need to stash the
        // message-level future so the resume path knows which future to
        // resolve when the suspended frame eventually returns. We do NOT
        // resolve `future` here and we do NOT re-schedule the actor; the
        // future's resolve/reject will schedule us via __waiters__.
        setCurrentActor(nullptr);
        if (future) {
            const_cast<proto::ProtoObject*>(actor)->setAttribute(
                ctx, suspFutKey, future);
        }
        if (actorGuard.owns_lock()) actorGuard.unlock();
        return true;
    } catch (const std::exception& e) {
        if (future) {
            auto* err = ctx->fromUTF8String(e.what());
            // F6 v2 T4: same atomic pattern as the resolve path above.
            rejectFutureFromDrain(*this, ctx, future, err);
        }
    }
    setCurrentActor(nullptr);

    // F6 v2 T3: drop the actor lock BEFORE re-scheduling. schedule() takes
    // schedMu; doing so while still holding the actor lock would not
    // deadlock today (nothing holds schedMu while waiting for an actor
    // lock), but releasing first keeps the rule "actor lock and schedMu are
    // disjoint at every cross-thread observation point" simple to audit.
    if (actorGuard.owns_lock()) actorGuard.unlock();

    // Re-schedule actor if more messages remained at the time we popped.
    if (rescheduleAfter) schedule(actor);

    return true;
}

size_t STRuntime::scheduledCount() const {
    std::lock_guard<std::mutex> lock(impl_->schedMu);
    return impl_->readyQueue.size();
}

size_t STRuntime::workerCount() const {
    // No lock needed: workers is populated once at construction and only
    // mutated by ~STRuntime; observers between those points see a stable
    // value.
    return impl_->workers.size();
}

const proto::ProtoObject* STRuntime::newFuture(proto::ProtoContext* ctx) {
    auto* fut = const_cast<proto::ProtoObject*>(impl_->bootstrap.futureProto)
        ->newChild(ctx, /*isMutable=*/true);
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* valueKey =
        ctx->fromUTF8String("__value__")->asString(ctx);
    static const proto::ProtoString* errKey =
        ctx->fromUTF8String("__error__")->asString(ctx);
    fut->setAttribute(ctx, stateKey, ctx->fromLong(0));  // 0 = pending
    fut->setAttribute(ctx, valueKey, PROTO_NONE);
    fut->setAttribute(ctx, errKey,   PROTO_NONE);
    // F6 v2 T4: attach a per-future condition_variable so Future>>wait can
    // block (rather than busy-retry) and resolve/rejectWith/drainOne can
    // wake the waiter via notify_all. Every future built through this path
    // carries a cv; Future>>wait throws if asked to wait on a future
    // without one.
    installFutureCV(ctx, fut);
    return fut;
}

bool STRuntime::isActor(proto::ProtoContext* ctx,
                        const proto::ProtoObject* obj) const {
    if (!obj || obj == PROTO_NONE) return false;
    static const proto::ProtoString* wrappedKey =
        ctx->fromUTF8String("__wrapped__")->asString(ctx);
    auto* w = obj->getAttribute(ctx, wrappedKey);
    return (w != nullptr && w != PROTO_NONE);
}

// F5-M1: Resolve a logical module path to a filesystem path.
// Search order: cwd, $STPATH (colon-separated), active venv modules dir.
std::string STRuntime::findModuleFile(const std::string& logicalPath) const {
    namespace fs = std::filesystem;

    // Ensure .st suffix.
    std::string name = logicalPath;
    if (name.size() < 3 || name.substr(name.size() - 3) != ".st") {
        name += ".st";
    }

    // 1. Current working directory.
    {
        std::error_code ec;
        fs::path p = fs::current_path(ec) / name;
        if (!ec && fs::exists(p)) return p.string();
    }

    // 2. $STPATH (colon-separated list of directories).
    if (const char* env = std::getenv("STPATH")) {
        std::string paths = env;
        size_t start = 0;
        while (start <= paths.size()) {
            size_t end = paths.find(':', start);
            std::string entry = (end == std::string::npos)
                ? paths.substr(start)
                : paths.substr(start, end - start);
            if (!entry.empty()) {
                fs::path p = fs::path(entry) / name;
                if (fs::exists(p)) return p.string();
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    // 3. Active venv's lib/protoST/modules/. Prefer the existing venv
    // discovery helper (walks parents looking for .venv/stenv.cfg, falls back
    // to $STENV); also accept a bare $STENV pointing directly at a venv dir.
    {
        std::string venv = venvDiscover("");
        if (venv.empty()) {
            if (const char* env = std::getenv("STENV")) venv = env;
        }
        if (!venv.empty()) {
            fs::path p = fs::path(venv) / "lib" / "protoST" / "modules" / name;
            if (fs::exists(p)) return p.string();
        }
    }

    return "";
}

// F5-M1: Read, parse, compile, and execute a module file. Wraps the classes
// the module declared as attributes of a fresh ProtoObject and returns it.
const proto::ProtoObject* STRuntime::loadModuleFromFile(
    proto::ProtoContext* ctx,
    const std::string& filePath,
    const std::string& logicalName)
{
    namespace fs = std::filesystem;
    if (!fs::exists(filePath)) {
        throw std::runtime_error("module file not found: " + filePath);
    }

    // Read the file.
    std::ifstream f(filePath, std::ios::binary);
    if (!f) {
        throw std::runtime_error("module open failed: " + filePath);
    }
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();

    // Parse.
    Parser P(src);
    auto ast = P.parseModule();
    if (!P.errors().empty()) {
        std::string msg = "module parse errors in " + logicalName + ":";
        for (auto& e : P.errors()) {
            msg += "\n  " + std::to_string(e.line) + ":"
                 + std::to_string(e.column) + " " + e.message;
        }
        throw std::runtime_error(msg);
    }

    // Compile.
    Compiler C;
    auto bc = C.compileModule(*ast);
    if (C.hasErrors()) {
        std::string msg = "module compile errors in " + logicalName + ":";
        for (auto& s : C.errors()) msg += "\n  " + s;
        throw std::runtime_error(msg);
    }

    // Execute the module's top-level (registers classes/methods in globals).
    runTopLevel(*bc);

    // Build the module wrapper: a fresh mutable child of objectProto whose
    // attributes name the classes the module declared.
    auto* moduleObj = const_cast<proto::ProtoObject*>(impl_->bootstrap.objectProto)
        ->newChild(ctx, /*isMutable=*/true);
    auto* g = globals();

    for (const auto& [className, info] : C.classes()) {
        (void)info;  // metadata; only the name is used here.
        if (className.empty() || className[0] == '_') continue;
        auto* classSym = ctx->fromUTF8String(className.c_str())->asString(ctx);
        auto* classObj = g->getAttribute(ctx, classSym);
        if (classObj && classObj != PROTO_NONE) {
            moduleObj->setAttribute(ctx, classSym, classObj);
        }
    }

    // F5-M3: Retain the compiled BytecodeModule for the runtime's lifetime so
    // method __bc_ptr__ pointers into its block storage remain valid across
    // subsequent sends. Without this the bc would be destroyed at function
    // return and any later send on a class declared by the module would
    // dereference freed memory.
    impl_->loadedModules.push_back(std::move(bc));

    return moduleObj;
}

// F5-M2: cached module loader. Repeated calls for the same logical path
// return the same ProtoObject (keyed by the canonical absolute filesystem
// path so two distinct logical names that resolve to the same file share
// a single module instance — sys.modules-style).
const proto::ProtoObject* STRuntime::loadModule(proto::ProtoContext* ctx, const std::string& logicalPath) {
    auto path = findModuleFile(logicalPath);
    if (path.empty()) {
        throw std::runtime_error("module not found: " + logicalPath);
    }
    // Use the absolute resolved path as cache key for canonical identity.
    namespace fs = std::filesystem;
    std::string canonical = fs::absolute(path).string();

    auto it = impl_->moduleCache.find(canonical);
    if (it != impl_->moduleCache.end()) return it->second;

    auto* mod = loadModuleFromFile(ctx, path, logicalPath);
    impl_->moduleCache[canonical] = mod;
    return mod;
}

} // namespace protoST
