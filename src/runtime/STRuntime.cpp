#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "ExecutionEngine.h"
#include "FutureYield.h"
#include "NonLocalReturn.h"
#include "UnwindToHandler.h"
#include "ResumeSignal.h"
#include "RetrySignal.h"
#include "PassSignal.h"
#include "BytecodeModule.h"
#include "Bootstrap.h"
#include "Venv.h"
#include "SchedDiag.h"
#include "GcSafeBlocking.h"
#include "GcSafeMutex.h"
#include "TransientPin.h"
#include "UnhandledSTException.h"
#include "NativeExceptionBridge.h"
#include "debugger/DebuggerRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "modules/STModuleProvider.h"
#include "protoCore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
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
namespace protoST { void installMathPrimitives(STRuntime& rt); }
namespace protoST { void installTimePrimitives(STRuntime& rt); }
namespace protoST { void installBoolPrimitives(STRuntime& rt); }
namespace protoST { void installStringPrimitives(STRuntime& rt); }
namespace protoST { void installBlockPrimitives(STRuntime& rt); }
namespace protoST { void installDebuggerPrimitives(STRuntime& rt); }
namespace protoST { void installObjectPrimitives(STRuntime& rt); }
namespace protoST { void installFuturePrimitives(STRuntime& rt); }
namespace protoST { void installAtomPrimitives(STRuntime& rt); }
namespace protoST { void installImportGlobal(STRuntime& rt); }
namespace protoST { void installExceptionPrimitives(STRuntime& rt); }
namespace protoST { void installCollectionPrimitives(STRuntime& rt); }

// F6-A6: future transition helpers defined alongside the Future primitives.
// resolveFutureFromDrain / rejectFutureFromDrain perform the lock-free
// settle sequence (claim via setAttributeIfEqual, write value, fire
// callbacks, publish state, schedule waiters). drainOne calls these instead
// of writing state and firing callbacks itself.
namespace protoST {
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
    // F6 v3 E5: intern the selector as a STRONG SYMBOL via createSymbol. A
    // strong symbol is eternal — recorded in the per-space SymbolTable and
    // never reclaimed by the GC — so the method binding installed under it
    // can never lose its key. Previously this used fromUTF8String()->asString
    // which, for selectors longer than 6 bytes (INLINE_STRING_MAX_BYTES),
    // produced an ordinary heap ProtoString reachable from no GC root: a GC
    // cycle could reclaim it, leaving the method effectively un-findable
    // (observed as a deep-chain `doesNotUnderstand`). The SEND-dispatch site
    // (ExecutionEngine.cpp) now also interns selectors via createSymbol, so
    // both sides agree on the same eternal symbol.
    auto* sel = proto::ProtoString::createSymbol(ctx, selector);
    // Tag bit 62 marks "this is a primitive marker, not a real method object".
    auto* val = ctx->fromLong(static_cast<long long>(idx) | (1LL << 62));
    const_cast<proto::ProtoObject*>(proto)->setAttribute(ctx, sel, val);
}

// F6 v3 E2b: derive a unique ProtoString attribute key from an object
// pointer. The live registry (a mutable ProtoObject) stores each anchored
// object under such a key so the entry can later be removed by pointer
// identity. Pointer identity is stable for a live object — exactly what is
// needed since the object is kept alive precisely while the entry exists.
const proto::ProtoString*
ptrRegistryKey(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "p%llx",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(o)));
    return ctx->fromUTF8String(buf)->asString(ctx);
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

    // F6 scheduler — lock-free. The ready queue and the per-actor
    // "scheduled" state live in protoCore objects mutated by compare-and-swap,
    // not in C++ containers — so there is no scheduler mutex and no condition
    // variable. The ready queue is the ProtoList under `liveRegistry.__ready__`;
    // the per-actor turn-ownership flag is the `__sched__` attribute (3-state:
    // 0 idle / 1 active / 2 active+wakeup-pending). See STRuntime.h
    // (enqueueReady / dequeueReady / schedState) and workerLoop.

    // F6 v3 E2b: single live-registry GC root for all transient liveness.
    //
    // protoCore's tracing GC is designed so that to keep transient objects
    // alive you hang them off ONE already-pinned structure — a single root
    // reference is enough, since the collector traverses everything reachable
    // from that root during marking. We therefore do NOT mint a ProtoRootSet
    // handle per object. Instead `liveRegistry` is a single mutable
    // ProtoObject pinned once in asyncRoots for the runtime's whole lifetime;
    // every transient object that must survive GC is added as an attribute of
    // it (keyed by the object's pointer) and removed when it no longer needs
    // pinning.
    //
    // It anchors:
    //  - scheduled actors. The ready queue (`liveRegistry.__ready__`) is a
    //    protoCore ProtoList and is itself traced, so a queued actor is
    //    reachable through it; the per-pointer registry entry keeps it
    //    anchored across the brief schedule()..enqueue window too;
    //  - cooperatively-suspended actors, parked on an awaited future's
    //    __waiters__ list between yield and resume. In a deep dependency
    //    chain EVERY actor can be suspended at once with no live readyQueue
    //    entry anchoring any of them; keeping each one in the registry from
    //    its first schedule() until final completion bridges that gap.
    //  - the module-level captured-locals dict during runTopLevel;
    //  - the runtime-wide permanent prototypes + globals.
    //
    // Registry mutations are plain setAttribute / removeAttribute on this
    // mutable object — protoCore's per-attribute CAS makes concurrent
    // mutations atomic with no external lock (see registryAdd / registryRemove).
    //
    // `workers` holds N managed ProtoThreads spawned in the STRuntime
    // constructor (F6 v2 T7). Each gets its own root ProtoContext chain and
    // independently drains the shared lock-free ready queue. The pool
    // size is hardware_concurrency() (defaulted to 2, capped at 8); the
    // PROTOST_WORKERS env var overrides it for tests + experimentation.
    // `shutdown` is set by ~STRuntime; each worker observes it on its next
    // poll iteration (within the backoff interval) and exits.
    const proto::ProtoObject* liveRegistry = nullptr;

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
        auto* objKey = proto::ProtoString::createSymbol(rootCtx, "Object");
        globals->setAttribute(rootCtx, objKey, bootstrap.objectProto);

        // F6: register Actor and Future in globals so user code can refer to
        // them via PUSH_GLOBAL (e.g. `Actor subclass: ...`, `Future new`).
        auto* actorKey = proto::ProtoString::createSymbol(rootCtx, "Actor");
        globals->setAttribute(rootCtx, actorKey, bootstrap.actorProto);

        auto* futureKey = proto::ProtoString::createSymbol(rootCtx, "Future");
        globals->setAttribute(rootCtx, futureKey, bootstrap.futureProto);

        // Atom — the shared mutable cell with optimistic-concurrency CAS.
        auto* atomKey = proto::ProtoString::createSymbol(rootCtx, "Atom");
        globals->setAttribute(rootCtx, atomKey, bootstrap.atomProto);

        // Track 1 slice 2 (EXC-a): register the exception class hierarchy in
        // globals so user code can name `Exception`, `Error`, `Warning` and
        // do `Exception subclass: #MyError`.
        auto* exceptionKey = proto::ProtoString::createSymbol(rootCtx, "Exception");
        globals->setAttribute(rootCtx, exceptionKey, bootstrap.exceptionProto);
        auto* errorKey = proto::ProtoString::createSymbol(rootCtx, "Error");
        globals->setAttribute(rootCtx, errorKey, bootstrap.errorProto);
        auto* warningKey = proto::ProtoString::createSymbol(rootCtx, "Warning");
        globals->setAttribute(rootCtx, warningKey, bootstrap.warningProto);
        // MNT-b2 (D3 / D8): the two runtime-signalled Error subclasses are
        // nameable so a script can guard on them specifically
        // (`on: MessageNotUnderstood do:`), not only via the `Error` base.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "MessageNotUnderstood"),
            bootstrap.messageNotUnderstoodProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "BlockCannotReturn"),
            bootstrap.blockCannotReturnProto);

        // Track 2 slice a (COL-a): register the collection class hierarchy in
        // globals so user code can name `Collection`, `Array`, etc. and do
        // `Array new: 3` / `Array withAll: ...`.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Collection"),
            bootstrap.collectionProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "SequenceableCollection"),
            bootstrap.sequenceableCollectionProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "HashedCollection"),
            bootstrap.hashedCollectionProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Array"),
            bootstrap.arrayProto);
        // Track 2 slice b (COL-b): the growable sequenceable collection.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "OrderedCollection"),
            bootstrap.orderedCollectionProto);
        // Track 2 slice e (COL-e): the lazy interval collection.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Interval"),
            bootstrap.intervalProto);
        // Track 2 slice c (COL-c): the hashed collections.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Set"),
            bootstrap.setProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Bag"),
            bootstrap.bagProto);
        // Track 2 slice d (COL-d): the key->value map and the key->value pair.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Dictionary"),
            bootstrap.dictionaryProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Association"),
            bootstrap.associationProto);

        // Track 4 slice b (T4-b): register the numeric class hierarchy in
        // globals so user code can name `Number`, `SmallInteger`,
        // `LargeInteger` and `Float`. `Float` in particular carries the
        // class-side math constants — `Float pi`, `Float e`, `Float infinity`,
        // `Float nan` — bound by installMathPrimitives.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Number"),
            bootstrap.numberProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "SmallInteger"),
            bootstrap.smallIntegerProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "LargeInteger"),
            bootstrap.largeIntegerProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Float"),
            bootstrap.floatProto);

        // Register `String` and `Boolean` in globals so user code (and stdlib
        // modules) can name them — to extend them with `String >> selector`
        // double-dispatch methods, the same way `Number` is nameable. Their
        // prototypes already exist; only the global binding was missing.
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "String"),
            bootstrap.stringProto);
        globals->setAttribute(rootCtx,
            proto::ProtoString::createSymbol(rootCtx, "Boolean"),
            bootstrap.booleanProto);

        // F6 v3 E2b: create the single live-registry GC root and pin it.
        //
        // This is the ONLY object ever handed to asyncRoots->add(). Every
        // other liveness need — transient actors and the permanent
        // prototypes/globals below — is expressed as an attribute of this
        // mutable ProtoObject, so the tracing collector reaches them all from
        // this one root. The pin is never released before ~Impl destroys the
        // whole root set.
        if (asyncRoots) {
            liveRegistry = bootstrap.objectProto->newChild(rootCtx, /*isMutable=*/true);
            asyncRoots->add(liveRegistry);
            // The lock-free ready queue lives here, under `__ready__`, as a
            // protoCore ProtoList mutated by CAS. Initialise it to an empty
            // list once so enqueue / dequeue always CAS against a real list.
            const_cast<proto::ProtoObject*>(liveRegistry)->setAttribute(
                rootCtx, bootstrap.sym.ready,
                rootCtx->newList()->asObject(rootCtx));
        }

        // F6 v3 E2: anchor the runtime-wide GC roots in the live registry.
        //
        // protoCore's tracing GC marks from a fixed set of roots: the
        // built-in `space->*Prototype` slots, registered ProtoRootSets, and
        // live thread contexts. protoST builds its own Smalltalk prototype
        // tree (objectProto, actorProto, futureProto, numberProto, ...) as
        // children of the protoCore prototypes, and a `globals` namespace
        // holding every user-declared class. bootstrapPrototypes re-points a
        // FEW protoCore slots at protoST protos (smallInteger, string, ...),
        // so those are transitively rooted — but `objectProto`, `numberProto`,
        // `symbolProto`, `blockProto`, `actorProto`, `futureProto` and
        // `globals` are reachable from NO root. Tracing runs parent→reference,
        // never parent→child, so a protoCore prototype does not keep its
        // protoST child alive.
        //
        // As long as GC never ran (it is deferred until allocation pressure
        // builds) this was invisible. Once a deep workload triggers a cycle,
        // the collector reclaims these unrooted prototypes and the method
        // bindings installed on them — observed as spurious `doesNotUnderstand`
        // (e.g. `+` on an integer, `linkTo:`/`compute` on an actor) after the
        // first GC cycle. Adding them to the live registry for the runtime's
        // whole lifetime closes that gap. These entries are never removed —
        // they are live for exactly as long as the runtime is.
        if (liveRegistry) {
            auto pinPermanent = [this](const proto::ProtoObject* o) {
                if (o && o != PROTO_NONE) {
                    const_cast<proto::ProtoObject*>(liveRegistry)
                        ->setAttribute(rootCtx, ptrRegistryKey(rootCtx, o), o);
                }
            };
            pinPermanent(globals);
            pinPermanent(bootstrap.objectProto);
            pinPermanent(bootstrap.numberProto);
            pinPermanent(bootstrap.smallIntegerProto);
            pinPermanent(bootstrap.largeIntegerProto);
            pinPermanent(bootstrap.floatProto);
            pinPermanent(bootstrap.booleanProto);
            pinPermanent(bootstrap.stringProto);
            pinPermanent(bootstrap.symbolProto);
            pinPermanent(bootstrap.blockProto);
            pinPermanent(bootstrap.actorProto);
            pinPermanent(bootstrap.futureProto);
            pinPermanent(bootstrap.nilProto);
            // Track 1 slice 2 (EXC-a): the exception prototypes carry the
            // signal / on:do: / return: bindings — pin them for the runtime's
            // whole lifetime, exactly like the other built-in classes.
            pinPermanent(bootstrap.exceptionProto);
            pinPermanent(bootstrap.errorProto);
            pinPermanent(bootstrap.warningProto);
            // MNT-b2 (D3 / D8): the runtime-signalled Error subclasses carry
            // method bindings inherited from Error — pin them for the
            // runtime's whole lifetime, like every other built-in class.
            pinPermanent(bootstrap.messageNotUnderstoodProto);
            pinPermanent(bootstrap.blockCannotReturnProto);
            // Track 2 slice a (COL-a): the collection prototypes carry the
            // iteration protocol + Array base operations — pin them for the
            // runtime's whole lifetime, exactly like the other built-in classes.
            pinPermanent(bootstrap.collectionProto);
            pinPermanent(bootstrap.sequenceableCollectionProto);
            pinPermanent(bootstrap.hashedCollectionProto);
            pinPermanent(bootstrap.arrayProto);
            // Track 2 slice b (COL-b): the growable sequenceable collection.
            pinPermanent(bootstrap.orderedCollectionProto);
            // Track 2 slice e (COL-e): the lazy `Interval` carries its base
            // operations and `Number>>to:` builds instances — pin it for the
            // runtime's whole lifetime, like every other built-in class.
            pinPermanent(bootstrap.intervalProto);
            // Track 2 slice c (COL-c): the hashed collections — Set / Bag carry
            // their base operations; pin them for the runtime's whole lifetime.
            pinPermanent(bootstrap.setProto);
            pinPermanent(bootstrap.bagProto);
            // Track 2 slice d (COL-d): the Dictionary carries its base
            // operations, Association the key->value accessors — pin both for
            // the runtime's whole lifetime, like every other built-in class.
            pinPermanent(bootstrap.dictionaryProto);
            pinPermanent(bootstrap.associationProto);
        }
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
    installMathPrimitives(*this);
    installTimePrimitives(*this);
    installBoolPrimitives(*this);
    installStringPrimitives(*this);
    installBlockPrimitives(*this);
    installDebuggerPrimitives(*this);
    installObjectPrimitives(*this);
    installFuturePrimitives(*this);
    installAtomPrimitives(*this);
    installExceptionPrimitives(*this);
    installCollectionPrimitives(*this);
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
                proto::ProtoString::createSymbol(ctx, nameStr.c_str());
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
        // No cv to notify — each worker observes `shutdown` on its next poll
        // iteration (within the backoff interval) and exits workerLoop.
        // F6 v3 E5: the join() loop blocks the main thread in the kernel
        // (pthread_join) while it is STILL counted in
        // ProtoSpace::runningThreads. This is the same off-safepoint-blocking
        // hazard E2/E4 closed for cv sleeps and mutex acquisition — and a
        // genuine deadlock the deep-chain audit uncovered:
        //
        //   * a worker still inside drainOne hits allocCell, sees stwFlag set
        //     and PARKS for a stop-the-world cycle (parkedThreads++);
        //   * the GC thread's Phase-1 quorum is `parkedThreads >=
        //     runningThreads`, but the main thread, blocked in join() and
        //     never reaching a safepoint, keeps runningThreads above
        //     parkedThreads forever;
        //   * the GC never finishes, the parked worker never wakes, never
        //     exits its loop, and join() never returns. Total deadlock.
        //
        // Bracketing the whole join loop in a GC-blocking region removes the
        // main thread from the running set for its duration, so the STW
        // quorum is computed only over threads that can actually park. No
        // protoCore heap access happens inside the region (join() is a pure
        // pthread wait), and no protoST lock is held across exitGcBlocking —
        // both GcSafeBlocking rules are honoured.
        enterGcBlocking(impl_->rootCtx);
        for (auto* t : impl_->workers) {
            // ProtoThread::join takes the CALLING context (main thread's
            // root). newThread returns const ProtoThread*; join is non-const.
            const_cast<proto::ProtoThread*>(t)->join(impl_->rootCtx);
        }
        exitGcBlocking(impl_->rootCtx);
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
    // The lock-free scheduler has no condition variable to park on. A caller
    // (a Future>>wait drive loop, a test) just needs a bounded GC-safe pause
    // before re-polling. Bracket the sleep in a GC-blocking region so this
    // thread leaves the running set for its duration (it must not stall the
    // stop-the-world quorum off-safepoint).
    auto* ctx = impl_->rootCtx;
    enterGcBlocking(ctx);
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
    exitGcBlocking(ctx);
    return false;  // no progress signal — the caller re-checks its condition
}

void STRuntime::workerLoop(proto::ProtoContext* ctx) {
    // Poll the lock-free ready queue. drainOne CAS-pops the head; when the
    // queue is empty the worker backs off with a GC-safe bounded sleep
    // (bracketed by enterGcBlocking/exitGcBlocking so an idle worker leaves
    // the running set and never stalls a stop-the-world GC — the same E2
    // discipline the old cv sleep used). With no mutex anywhere on this path
    // the D23 deadlock class (a worker blocking off-safepoint on schedMu
    // while another parks at a GC safepoint owning it) cannot occur.
    int idle = 0;
    while (true) {
        if (impl_->shutdown.load(std::memory_order_acquire)) {
            // Drain whatever is left, then exit once the queue is empty so no
            // pending work is lost (the destructor join() waits for this).
            if (!drainOne(ctx)) return;
            continue;
        }
        if (drainOne(ctx)) { idle = 0; continue; }
        // Nothing to drain — back off. The interval ramps so a busy pool
        // (queue never empties) never sleeps, while a fully idle pool settles
        // at a low wake rate.
        ++idle;
        unsigned ms = idle < 8 ? 1u : (idle < 64 ? 4u : 16u);
        enterGcBlocking(ctx);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        exitGcBlocking(ctx);
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
            // F6 v3 E5: a Smalltalk symbol literal (`#foo`) is interned as a
            // STRONG SYMBOL via createSymbol — eternal, recorded in the
            // per-space SymbolTable, never GC-reclaimed. This matters because
            // these materialised symbols are used as attribute KEYS (notably
            // the selector argument of `__installMethod:as:`); an ordinary
            // heap ProtoString key would be reachable from no GC root once it
            // left the operand stack and could be reclaimed mid-program.
            return reinterpret_cast<const proto::ProtoObject*>(
                proto::ProtoString::createSymbol(ctx, m.constSymbol(i).c_str()));
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
    // F6 v3 E5: `capturedDict` is unrooted between this newChild and the
    // registryAdd below — and registryAdd itself allocates (ptrRegistryKey +
    // setAttribute) before `capturedDict` lands in liveRegistry. Pin it for
    // the brief pre-anchor window; once registryAdd returns, the liveRegistry
    // entry roots it and this pin is redundant but harmless.
    TransientPin pinCapturedDict(ctx, capturedDict);

    // F6 v3 E2b: anchor the module-level captured-locals dict in the live
    // registry for the whole top-level run.
    //
    // `capturedDict` holds every module-level variable — including, for the
    // cooperative-chain workloads, all N actor objects. While the top-level
    // script blocks in Future>>wait, the only C++-side reference to
    // `capturedDict` is this ExecutionEngine's `frames_` vector, which is a
    // plain std::vector the tracing GC cannot see. A GC cycle that fires
    // while the main thread is parked in `wait` (now reachable since the
    // GC-starvation deadlock is fixed) would otherwise reclaim `capturedDict`
    // and every actor/class hanging off it — observed as `doesNotUnderstand`
    // once the chain is deep enough to trigger a cycle mid-run. Adding it to
    // the live registry for the lifetime of runTopLevel closes that window;
    // the RAII guard below removes the entry on every exit path (exceptions
    // included).
    registryAdd(ctx, capturedDict);
    struct CapturedAnchorGuard {
        STRuntime* self;
        proto::ProtoContext* ctx;
        const proto::ProtoObject* dict;
        ~CapturedAnchorGuard() { self->registryRemove(ctx, dict); }
    } capturedAnchorGuard{this, ctx, capturedDict};

    // Track 1 slice 1: a NonLocalReturn that escapes the top-level engine has
    // no live home frame anywhere — the block's home method already returned
    // (a "dead home"). Convert it to a std::runtime_error so the REPL / `-e`
    // / script callers render it the same way as any other runtime fault.
    try {
        return eng.runWithArgs(ctx, m, /*self=*/PROTO_NONE,
                               /*args=*/nullptr, /*argc=*/0, capturedDict);
    } catch (const NonLocalReturn&) {
        throw std::runtime_error(
            "non-local return: home method has already returned");
    } catch (const UnwindToHandler&) {
        // Track 1 slice 2 (EXC-a): an UnwindToHandler reached the top level
        // with no `on:do:` to catch it. A balanced `on:do:` always catches
        // its own id, so this is a bug (an exception handler ran but its
        // owning `on:do:` was already gone). Surface it as a runtime error.
        throw std::runtime_error(
            "exception unwind: no matching on:do: handler activation");
    } catch (const RetrySignal&) {
        // EXC-b: a `retry` reached the top level with no `on:do:` to re-enter
        // — same bug class as a stray UnwindToHandler.
        throw std::runtime_error(
            "exception retry: no matching on:do: handler activation");
    } catch (const ResumeSignal&) {
        // EXC-b: a `resume:` escaped its `signal` loop — a bug; `signal`
        // always consumes the ResumeSignal for its own id.
        throw std::runtime_error(
            "exception resume: no active signal to resume");
    } catch (const PassSignal&) {
        // EXC-b: a `pass` escaped its `signal` loop — a bug.
        throw std::runtime_error(
            "exception pass: no active signal to pass");
    }
}

// F6 v3 E2b: anchor `o` in the live registry so the tracing GC reaches it
// from the single pinned root. Idempotent — setAttribute with the same
// pointer-derived key simply overwrites. Serialized under schedMu, the same
// lock guarding readyQueue / scheduledSet, so concurrent schedule / drain
// callers never race on the registry. Callers MUST NOT already hold schedMu.
void STRuntime::registryAdd(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!o || o == PROTO_NONE || !impl_->liveRegistry) return;
    // No lock: setAttribute on a mutable object is internally atomic
    // (protoCore's per-attribute shard CAS), so concurrent registryAdd /
    // registryRemove / enqueueReady calls on liveRegistry never corrupt it.
    // F6 v3 E5: ptrRegistryKey allocates a fresh ProtoString; holding it in a
    // C++ temporary across setAttribute (which allocates a sparse-list node on
    // the mutable liveRegistry) is a transient-across-allocation gap. Pin the
    // key. `o` is the object being anchored — it is the caller's already-live
    // value and gets permanently rooted by the setAttribute itself.
    auto* key = ptrRegistryKey(ctx, o);
    TransientPin pinKey(ctx, reinterpret_cast<const proto::ProtoObject*>(key));
    const_cast<proto::ProtoObject*>(impl_->liveRegistry)
        ->setAttribute(ctx, key, o);
}

// F6 v3 E2b: drop `o` from the live registry. Balanced against registryAdd;
// safe to call for an object that was never added (removeAttribute of a
// missing key is a no-op). Callers MUST NOT already hold schedMu.
void STRuntime::registryRemove(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!o || o == PROTO_NONE || !impl_->liveRegistry) return;
    // No lock — see registryAdd (per-attribute CAS makes this atomic).
    // F6 v3 E5: same transient-key gap as registryAdd — removeAttribute on a
    // mutable object also allocates (a new sparse-list node for the smaller
    // tree). Pin the key across the call.
    auto* key = ptrRegistryKey(ctx, o);
    TransientPin pinKey(ctx, reinterpret_cast<const proto::ProtoObject*>(key));
    const_cast<proto::ProtoObject*>(impl_->liveRegistry)
        ->removeAttribute(ctx, key);
}

// ---------------------------------------------------------------------------
// Lock-free scheduler primitives. The ready queue is the ProtoList under
// liveRegistry.__ready__; each op is a CAS-retry over that one attribute —
// the same lock-free pattern as the actor mailbox. The per-actor `__sched__`
// flag is a SmallInteger CAS'd via setAttributeIfEqual.
// ---------------------------------------------------------------------------

void STRuntime::enqueueReady(proto::ProtoContext* ctx,
                             const proto::ProtoObject* actor) {
    if (!actor || !impl_->liveRegistry) return;
    auto* reg = const_cast<proto::ProtoObject*>(impl_->liveRegistry);
    const proto::ProtoString* readyKey = impl_->bootstrap.sym.ready;
    for (;;) {
        const proto::ProtoObject* cur = reg->getOwnAttributeDirect(ctx, readyKey);
        const proto::ProtoList* curList =
            (cur && cur != PROTO_NONE) ? cur->asList(ctx) : ctx->newList();
        const proto::ProtoList* nextList = curList->appendLast(ctx, actor);
        // `nextList` / its object form are held across the CAS — pin them.
        TransientPin pinNext(
            ctx, reinterpret_cast<const proto::ProtoObject*>(nextList));
        const proto::ProtoObject* nextObj = nextList->asObject(ctx);
        TransientPin pinNextObj(ctx, nextObj);
        if (reg->setAttributeIfEqual(ctx, readyKey, cur, nextObj)) return;
    }
}

const proto::ProtoObject* STRuntime::dequeueReady(proto::ProtoContext* ctx) {
    if (!impl_->liveRegistry) return nullptr;
    auto* reg = const_cast<proto::ProtoObject*>(impl_->liveRegistry);
    const proto::ProtoString* readyKey = impl_->bootstrap.sym.ready;
    for (;;) {
        const proto::ProtoObject* cur = reg->getOwnAttributeDirect(ctx, readyKey);
        if (!cur || cur == PROTO_NONE) return nullptr;
        const proto::ProtoList* curList = cur->asList(ctx);
        if (!curList) return nullptr;
        long long n = curList->getSize(ctx);
        if (n == 0) return nullptr;
        const proto::ProtoObject* head = curList->getAt(ctx, 0);
        const proto::ProtoList* rest =
            curList->getSlice(ctx, 1, static_cast<int>(n));
        TransientPin pinHead(ctx, head);
        TransientPin pinRest(
            ctx, reinterpret_cast<const proto::ProtoObject*>(rest));
        const proto::ProtoObject* restObj = rest->asObject(ctx);
        TransientPin pinRestObj(ctx, restObj);
        if (reg->setAttributeIfEqual(ctx, readyKey, cur, restObj)) return head;
    }
}

long long STRuntime::schedState(proto::ProtoContext* ctx,
                                const proto::ProtoObject* actor) {
    const proto::ProtoObject* s =
        actor->getOwnAttributeDirect(ctx, impl_->bootstrap.sym.sched);
    return (s && s != PROTO_NONE) ? s->asLong(ctx) : 0;
}

bool STRuntime::casSchedState(proto::ProtoContext* ctx,
                              const proto::ProtoObject* actor,
                              long long from, long long to) {
    auto* a = const_cast<proto::ProtoObject*>(actor);
    const proto::ProtoString* k = impl_->bootstrap.sym.sched;
    if (from == 0) {
        // "idle" is either an explicit SmallInteger 0 or the `__sched__`
        // attribute being ABSENT — an actor not built through the asActor
        // primitive (e.g. a unit-test fixture) starts without it. Accept
        // both: if currently absent, CAS against nullptr (= "absent"); the
        // CAS itself is the atomic check, so a racing writer is handled by
        // the caller's retry loop.
        const proto::ProtoObject* cur = actor->getOwnAttributeDirect(ctx, k);
        if (!cur || cur == PROTO_NONE)
            return a->setAttributeIfEqual(ctx, k, nullptr, ctx->fromLong(to));
    }
    return a->setAttributeIfEqual(ctx, k, ctx->fromLong(from), ctx->fromLong(to));
}

bool STRuntime::mailboxHasWork(proto::ProtoContext* ctx,
                               const proto::ProtoObject* actor) {
    const proto::ProtoObject* mb =
        actor->getOwnAttributeDirect(ctx, impl_->bootstrap.sym.mailbox);
    if (!mb || mb == PROTO_NONE) return false;
    const proto::ProtoList* mbList = mb->asList(ctx);
    return mbList && mbList->getSize(ctx) > 0;
}

void STRuntime::schedule(proto::ProtoContext* ctx, const proto::ProtoObject* actor) {
    if (!actor) return;
    // Anchor the actor in the live registry first (idempotent overwrite of
    // the same pointer-key); the ready ProtoList traces it too once enqueued.
    registryAdd(ctx, actor);
    // Drive the 3-state __sched__ flag:
    //   0 -> 1  we claim the actor and enqueue it;
    //   1 -> 2  the actor is mid-turn — mark a pending wakeup, finishDrain
    //           will re-queue it;
    //   2       a wakeup is already marked — nothing to do.
    for (;;) {
        long long s = schedState(ctx, actor);
        if (s == 2) {
            SCHED_DIAG("schedule actor=" << actor << " already marked (2)");
            return;
        }
        if (s == 1) {
            if (casSchedState(ctx, actor, 1, 2)) {
                SCHED_DIAG("schedule actor=" << actor << " marked wakeup (1->2)");
                return;
            }
            continue;  // flag changed under us — retry
        }
        // s == 0
        if (casSchedState(ctx, actor, 0, 1)) {
            enqueueReady(ctx, actor);
            SCHED_DIAG("schedule actor=" << actor << " enqueued (0->1)");
            return;
        }
        continue;  // flag changed under us — retry
    }
}

bool STRuntime::drainOne(proto::ProtoContext* ctx) {
    // CAS-pop the head of the lock-free ready queue. If two drainers race,
    // exactly one wins the head; the loser re-reads and gets the next actor
    // or an empty queue. The popped actor's `__sched__` flag is 1 or 2 — it
    // stays "owned by the scheduler" for the whole turn (turn-serialisation:
    // a SEND arriving mid-turn finds the flag non-zero and marks 1->2 rather
    // than re-enqueueing, so no second drainer can dispatch the same actor in
    // parallel). finishDrain (run by DrainGuard on every exit path) consumes
    // the flag — re-queueing on a 2, or on leftover mailbox work — once the
    // turn ends.
    const proto::ProtoObject* actor = dequeueReady(ctx);
    if (!actor) return false;
    SCHED_DIAG("drainOne POP actor=" << actor);

    // F6 v3 E2b: GC anchoring for the popped actor.
    //
    // The actor was added to the live registry by schedule() and is still
    // there. Its lifecycle within the registry is:
    //   - normal completion / exception → registryRemove (the actor no
    //     longer needs anchoring once its message is fully processed);
    //   - cooperative yield (FutureYield)  → keep it in the registry, so a
    //     fully-suspended deep chain still has every actor reachable from
    //     the single pinned root while parked on awaited futures'
    //     __waiters__ lists. The whoever-resolves path re-schedules it,
    //     which re-adds it harmlessly (idempotent overwrite of the same
    //     key). It is finally removed on the resume's completion path.
    //
    // Turn lifecycle guard: on EVERY exit path (return or caught throw) it
    // runs finishDrain, which under schedMu either re-queues the actor (its
    // mailbox still holds messages) or releases it from scheduledSet and the
    // live registry. A FutureYield sets `suspended = true` so finishDrain
    // instead just drops scheduledSet ownership and keeps the registry
    // anchor for the suspension window — whoever resolves the awaited future
    // reschedules the actor.
    struct DrainGuard {
        STRuntime* self;
        proto::ProtoContext* ctx;
        const proto::ProtoObject* actor;
        bool suspended = false;
        ~DrainGuard() { self->finishDrain(ctx, actor, suspended); }
    } drainGuard{this, ctx, actor};

    const proto::ProtoString* mailboxKey =
        impl_->bootstrap.sym.mailbox;
    const proto::ProtoString* wrappedKey =
        impl_->bootstrap.sym.wrapped;
    const proto::ProtoString* selKey =
        impl_->bootstrap.sym.selector;
    const proto::ProtoString* argsKey =
        impl_->bootstrap.sym.args;
    const proto::ProtoString* futKey =
        impl_->bootstrap.sym.future;
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
        impl_->bootstrap.sym.suspendedFrame;
    const proto::ProtoString* waitingOnKey =
        impl_->bootstrap.sym.waitingOn;
    const proto::ProtoString* suspFutKey =
        impl_->bootstrap.sym.suspendedFuture;
    const proto::ProtoString* fValueKey =
        impl_->bootstrap.sym.value;
    const proto::ProtoString* fErrorKey =
        impl_->bootstrap.sym.error;
    const proto::ProtoString* fStateKey =
        impl_->bootstrap.sym.state;
    // F6 v3 E5: these six keys are freshly interned ProtoStrings (per-space
    // interning forbids static caching) and are held in C++ locals across the
    // ENTIRE drainOne body — which runs a full ExecutionEngine (arbitrary
    // user-code allocation), resolveFutureFromDrain / rejectFutureFromDrain,
    // and many getAttribute/setAttribute calls. Pin all six for the function
    // scope. drainOne's `ctx` is a worker (or main) context; TransientPin
    // sizes it on demand if an engine has not already.
    TransientPin pinSuspKey(ctx, reinterpret_cast<const proto::ProtoObject*>(suspKey));
    TransientPin pinWaitKey(ctx, reinterpret_cast<const proto::ProtoObject*>(waitingOnKey));
    TransientPin pinSuspFutKey(ctx, reinterpret_cast<const proto::ProtoObject*>(suspFutKey));
    TransientPin pinFValKey(ctx, reinterpret_cast<const proto::ProtoObject*>(fValueKey));
    TransientPin pinFErrKey(ctx, reinterpret_cast<const proto::ProtoObject*>(fErrorKey));
    TransientPin pinFStKey(ctx, reinterpret_cast<const proto::ProtoObject*>(fStateKey));
    // F6 v2 T4: future state/value/error keys are no longer referenced here;
    // resolveFutureFromDrain / rejectFutureFromDrain in future_prims.cpp own
    // the entire transition including the attribute writes.

    // Turn serialisation — "at most one message in flight per actor" — no
    // longer needs a per-actor lock. Two hazards used to motivate one:
    //   1. SEND-append vs drainOne-pop racing on the mailbox itself. This is
    //      now closed by the lock-free CAS: both the SEND fast-path and the
    //      drainOne pop below use ProtoObject::setAttributeIfEqual in a retry
    //      loop, so the read-modify-write is atomic without any mutex.
    //   2. Two drainers dispatching the same actor in parallel. This is now
    //      closed by the scheduler: the actor stays in `scheduledSet` for the
    //      whole turn (it is not erased at pop), so a SEND arriving mid-turn
    //      finds it already scheduled and cannot re-enqueue it; finishDrain
    //      re-queues it once, after the turn, if its mailbox is non-empty.
    // A drainer therefore owns the actor for the turn by construction, and
    // per-actor instance state stays single-writer with no language-level
    // lock and no GC-safe-lock machinery on this path.

    // F6 v3 C+D: check for a suspended-frame snapshot BEFORE looking at the
    // mailbox. If present, this drainOne tick is a resume of a previously
    // yielded message rather than a fresh pop. We restore the snapshot into
    // a fresh ExecutionEngine, push the resolved value of the awaited
    // future onto the top frame's operand stack (matching what the
    // original `wait` would have returned), and let runLoop continue.
    {
        auto* snapAttr = actor->getAttribute(ctx, suspKey);
        SCHED_DIAG("drainOne actor=" << actor << " mode="
                   << ((snapAttr && snapAttr != PROTO_NONE) ? "RESUME"
                                                            : "FRESH-POP"));
        if (snapAttr && snapAttr != PROTO_NONE) {
            // F6 v3 E2b: this actor was suspended and is now being resumed.
            // It has stayed in the live registry the whole time (the yield
            // path's finishDrain(suspended) left the anchor in place), so no
            // re-add is needed here — the registry entry already roots it.
            // The DrainGuard for THIS drainOne tick will release it on normal
            // completion, or a re-yield below will set drainGuard.suspended.
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
                SCHED_DIAG("drainOne RESUME-COMPLETE actor=" << actor
                           << " msgFut=" << msgFut << " result=" << result);
                if (msgFut && msgFut != PROTO_NONE) {
                    // F6 v3 E5: `result` is the resumed run's final value —
                    // unrooted once continueRun has unwound; held across
                    // resolveFutureFromDrain (allocates). Pin it.
                    const proto::ProtoObject* rv =
                        result ? result : PROTO_NONE;
                    TransientPin pinResumeResult(ctx, rv);
                    resolveFutureFromDrain(*this, ctx, msgFut, rv);
                }
            } catch (const FutureYield&) {
                SCHED_DIAG("drainOne RESUME-REYIELD actor=" << actor);
                // Re-yield: the engine has already snapshotted + recorded
                // the new awaited future on the actor. Keep msgFut on the
                // actor for the next resume. Whoever resolves the new awaited
                // future will re-schedule us.
                setCurrentActor(nullptr);
                // Preserve __suspended_future__ so the next resume still
                // knows which message-level future to settle on completion.
                if (msgFut && msgFut != PROTO_NONE) {
                    const_cast<proto::ProtoObject*>(actor)->setAttribute(
                        ctx, suspFutKey, msgFut);
                }
                // The actor re-yielded — finishDrain(suspended) drops its
                // scheduledSet ownership and leaves the live-registry anchor
                // in place across the new suspension window.
                drainGuard.suspended = true;
                return true;
            } catch (const NonLocalReturn&) {
                // Track 1 slice 1: a non-local return escaped the resumed
                // engine with no live home frame — the home method already
                // returned during or before the yield window (a dead home).
                // Reject the message future the same way the fresh-handler
                // path does.
                SCHED_DIAG("drainOne RESUME DEAD-HOME actor=" << actor);
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    auto* err = ctx->fromUTF8String(
                        "non-local return: home method has already returned");
                    TransientPin pinErr(ctx, err);
                    rejectFutureFromDrain(*this, ctx, msgFut, err);
                }
            } catch (const UnwindToHandler&) {
                // Track 1 slice 2 (EXC-a): a stray UnwindToHandler escaped the
                // resumed engine — a balanced `on:do:` always catches its own
                // id, so this is a bug. Reject the message future, mirroring
                // the dead-home non-local-return path above.
                SCHED_DIAG("drainOne RESUME STRAY exception-unwind actor="
                           << actor);
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    auto* err = ctx->fromUTF8String(
                        "exception unwind: no matching on:do: handler activation");
                    TransientPin pinErr(ctx, err);
                    rejectFutureFromDrain(*this, ctx, msgFut, err);
                }
            } catch (const RetrySignal&) {
                // EXC-b: stray `retry` — same bug class as a stray unwind.
                SCHED_DIAG("drainOne RESUME STRAY exception-retry actor="
                           << actor);
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    auto* err = ctx->fromUTF8String(
                        "exception retry: no matching on:do: handler activation");
                    TransientPin pinErr(ctx, err);
                    rejectFutureFromDrain(*this, ctx, msgFut, err);
                }
            } catch (const ResumeSignal&) {
                // EXC-b: stray `resume:` — `signal` always consumes its own.
                SCHED_DIAG("drainOne RESUME STRAY exception-resume actor="
                           << actor);
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    auto* err = ctx->fromUTF8String(
                        "exception resume: no active signal to resume");
                    TransientPin pinErr(ctx, err);
                    rejectFutureFromDrain(*this, ctx, msgFut, err);
                }
            } catch (const PassSignal&) {
                // EXC-b: stray `pass` — `signal` always consumes its own.
                SCHED_DIAG("drainOne RESUME STRAY exception-pass actor="
                           << actor);
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    auto* err = ctx->fromUTF8String(
                        "exception pass: no active signal to pass");
                    TransientPin pinErr(ctx, err);
                    rejectFutureFromDrain(*this, ctx, msgFut, err);
                }
            } catch (const std::exception& e) {
                setCurrentActor(nullptr);
                if (msgFut && msgFut != PROTO_NONE) {
                    auto* err = ctx->fromUTF8String(e.what());
                    // F6 v3 E5: `err` held across rejectFutureFromDrain
                    // (allocates) — pin it.
                    TransientPin pinErr(ctx, err);
                    rejectFutureFromDrain(*this, ctx, msgFut, err);
                }
            }

            // The resume turn is over. Any mailbox messages that accumulated
            // during the yield window are handled by finishDrain (run by
            // DrainGuard): it re-queues the actor if its mailbox is non-empty,
            // keeping the registry anchor; otherwise it releases both.
            return true;
        }
    }

    const proto::ProtoObject* msg = nullptr;
    {
        // Pop the FIFO head off the mailbox with a lock-free compare-and-swap
        // retry — the mirror of the SEND fast-path's CAS append. A concurrent
        // SEND appends to the tail; if it lands between our read and our
        // write the setAttributeIfEqual fails and we re-read. Re-reading is
        // safe: the head we want is still element 0 (a SEND only appends),
        // and getSlice(1, newSize) keeps whatever the SEND added.
        for (;;) {
            const proto::ProtoObject* mbObj =
                actor->getOwnAttributeDirect(ctx, mailboxKey);
            if (!mbObj || mbObj == PROTO_NONE) return true;
            auto* mailbox = mbObj->asList(ctx);
            if (!mailbox || mailbox->getSize(ctx) == 0) return true;

            msg = mailbox->getAt(ctx, 0);
            // F6 v3 E5: `remaining` is a freshly sliced list held across
            // asObject + the CAS. Pin it.
            auto* remaining = mailbox->getSlice(
                ctx, 1, static_cast<int>(mailbox->getSize(ctx)));
            TransientPin pinRemaining(
                ctx, reinterpret_cast<const proto::ProtoObject*>(remaining));
            const proto::ProtoObject* newMbObj = remaining->asObject(ctx);
            TransientPin pinNewMbObj(ctx, newMbObj);
            if (const_cast<proto::ProtoObject*>(actor)
                    ->setAttributeIfEqual(ctx, mailboxKey, mbObj, newMbObj))
                break;
        }
    }
    // F6 v3 E5: the setAttribute above overwrote the actor's __mailbox__
    // slot, so `msg` is no longer reachable via actor->__mailbox__. It is
    // held across the field-extraction getAttributes, the args-vector build,
    // and the full user-method ExecutionEngine run — and the message's
    // selector / args / future are reached transitively THROUGH `msg`. Pin it
    // for the rest of drainOne so the whole message envelope stays rooted.
    TransientPin pinMsg(ctx, msg);

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
            // F6 v3 E5: `err` held across rejectFutureFromDrain — pin it.
            TransientPin pinErr(ctx, err);
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
        const proto::ProtoString* bcKey =
            impl_->bootstrap.sym.bcPtr;
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
            const proto::ProtoString* capKey =
                impl_->bootstrap.sym.captured;
            auto* capDict = method->getAttribute(ctx, capKey);
            if (capDict == PROTO_NONE) capDict = nullptr;
            SCHED_DIAG("drainOne USER-METHOD ENTER actor=" << actor);
            ExecutionEngine subEng(*this);
            result = subEng.runWithArgs(
                ctx, *sub, /*self=*/wrapped,
                methodArgs.data(),
                static_cast<int>(methodArgs.size()),
                capDict);
            SCHED_DIAG("drainOne USER-METHOD EXIT actor=" << actor
                       << " result=" << result);
        } else if (method) {
            long long marker = method->asLong(ctx);
            if (marker & (1LL << 62)) {
                int idx = static_cast<int>(marker & ((1LL << 62) - 1));
                auto fn = impl_->registry.at(idx);
                // EXC-d: an actor's method dispatched as a native primitive is
                // a native call boundary — wrap it so a C++ exception from the
                // primitive becomes a catchable protoST Error. The wrapper
                // re-throws the control-flow siblings (FutureYield etc.) and
                // DebuggerHalt / UnhandledSTException untouched, so the
                // drainOne catch clauses below see exactly what they did
                // before for those types.
                result = translateNativeException(
                    *this, ctx,
                    [&] { return fn(*this, ctx, wrapped, args.data(), argc); });
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
            // F6 v3 E5: `result` is the user method's return value — a fresh
            // object reachable from no traced root once the engine that
            // produced it has unwound. It is held across resolveFutureFrom-
            // Drain, which writes future state, fires thenDo: callbacks (user
            // code) and schedules waiters — all of which allocate. Pin it.
            TransientPin pinResult(ctx, v);
            // F6 v2 T4: atomic (write state, snapshot cbs, notify cv)
            // under the future's mutex; thenDo: callbacks fire outside the
            // mutex on the snapshot. Closes the registration race where a
            // concurrent thenDo: could land its callback into __then_cbs__
            // AFTER drainOne had already taken its snapshot.
            resolveFutureFromDrain(*this, ctx, future, v);
        }
    } catch (const FutureYield&) {
        SCHED_DIAG("drainOne FRESH-YIELD actor=" << actor
                   << " msgFut=" << future);
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
        // The actor yielded — finishDrain(suspended) drops its scheduledSet
        // ownership and keeps the live-registry anchor for the suspension
        // window; without the anchor a fully-suspended chain would have no
        // live GC root and the tracing collector would reclaim it.
        drainGuard.suspended = true;
        return true;
    } catch (const NonLocalReturn&) {
        // Track 1 slice 1: a non-local return escaped the actor handler's
        // engine with no live home frame — the home method already returned
        // (a "dead home"). Reject the message's Future with the dead-home
        // error, the same way the synchronous top-level path surfaces it.
        SCHED_DIAG("drainOne DEAD-HOME non-local return actor=" << actor);
        setCurrentActor(nullptr);
        if (future) {
            auto* err = ctx->fromUTF8String(
                "non-local return: home method has already returned");
            TransientPin pinErr(ctx, err);
            rejectFutureFromDrain(*this, ctx, future, err);
        }
        return true;
    } catch (const UnwindToHandler&) {
        // Track 1 slice 2 (EXC-a): an UnwindToHandler escaped the actor
        // handler's engine with no `on:do:` to catch it. A balanced `on:do:`
        // always catches its own id, so this is a bug; reject the message's
        // Future with a clear error rather than letting a non-std::exception
        // escape drainOne and call std::terminate.
        SCHED_DIAG("drainOne STRAY exception-unwind actor=" << actor);
        setCurrentActor(nullptr);
        if (future) {
            auto* err = ctx->fromUTF8String(
                "exception unwind: no matching on:do: handler activation");
            TransientPin pinErr(ctx, err);
            rejectFutureFromDrain(*this, ctx, future, err);
        }
        return true;
    } catch (const RetrySignal&) {
        // EXC-b: a stray `retry` escaped the actor handler — a balanced
        // `on:do:` always catches its own id. Reject the message Future.
        SCHED_DIAG("drainOne STRAY exception-retry actor=" << actor);
        setCurrentActor(nullptr);
        if (future) {
            auto* err = ctx->fromUTF8String(
                "exception retry: no matching on:do: handler activation");
            TransientPin pinErr(ctx, err);
            rejectFutureFromDrain(*this, ctx, future, err);
        }
        return true;
    } catch (const ResumeSignal&) {
        // EXC-b: a stray `resume:` escaped its `signal` loop — a bug.
        SCHED_DIAG("drainOne STRAY exception-resume actor=" << actor);
        setCurrentActor(nullptr);
        if (future) {
            auto* err = ctx->fromUTF8String(
                "exception resume: no active signal to resume");
            TransientPin pinErr(ctx, err);
            rejectFutureFromDrain(*this, ctx, future, err);
        }
        return true;
    } catch (const PassSignal&) {
        // EXC-b: a stray `pass` escaped its `signal` loop — a bug.
        SCHED_DIAG("drainOne STRAY exception-pass actor=" << actor);
        setCurrentActor(nullptr);
        if (future) {
            auto* err = ctx->fromUTF8String(
                "exception pass: no active signal to pass");
            TransientPin pinErr(ctx, err);
            rejectFutureFromDrain(*this, ctx, future, err);
        }
        return true;
    } catch (const std::exception& e) {
        if (future) {
            auto* err = ctx->fromUTF8String(e.what());
            // F6 v3 E5: `err` held across rejectFutureFromDrain — pin it.
            TransientPin pinErr(ctx, err);
            // F6 v2 T4: same atomic pattern as the resolve path above.
            rejectFutureFromDrain(*this, ctx, future, err);
        }
    }
    setCurrentActor(nullptr);

    // The turn is over. finishDrain (run by DrainGuard) decides under schedMu
    // whether to re-queue the actor — if a SEND left messages in its mailbox
    // during this turn — or to release it from scheduledSet and the live
    // registry. No explicit reschedule here: doing it via schedule() would
    // race the still-present scheduledSet entry.
    return true;
}

void STRuntime::finishDrain(proto::ProtoContext* ctx,
                            const proto::ProtoObject* actor,
                            bool suspended) {
    if (!actor) return;

    // Drive the 3-state `__sched__` flag to its turn-end resolution. The flag
    // is 1 or 2 on entry (the actor was running this turn). There is no lock:
    // a concurrent SEND or future-settle calling schedule() either marks
    // 1->2 before our CAS 1->0 (so our CAS fails, we loop, see 2, re-queue)
    // or sees state 0 after our CAS and enqueues the actor itself. The CAS is
    // the linearisation point — a wakeup can never be stranded.
    for (;;) {
        long long s = schedState(ctx, actor);
        if (s == 2) {
            // A wakeup arrived during the turn — consume it and re-queue.
            if (casSchedState(ctx, actor, 2, 1)) {
                enqueueReady(ctx, actor);
                SCHED_DIAG("finishDrain RE-QUEUE (wakeup) actor=" << actor);
                return;
            }
            continue;  // raced — retry
        }
        // s == 1 — turn owner, no wakeup marked yet. Release the flag.
        if (!casSchedState(ctx, actor, 1, 0)) continue;  // became 2 — loop

        // Released (state 0). A SEND racing this CAS now sees 0 and enqueues
        // the actor itself; one that raced just before us marked 1->2 and the
        // CAS above would have failed. So from here we only need to handle
        // work this turn-owner already knows about: a completed turn whose
        // mailbox still holds messages (one turn drains one message).
        if (!suspended && mailboxHasWork(ctx, actor)) {
            // Re-claim and re-queue. If a concurrent schedule() already
            // re-claimed (0->1) and enqueued it, our CAS fails and we leave
            // it to them — enqueued exactly once either way.
            if (casSchedState(ctx, actor, 0, 1)) {
                enqueueReady(ctx, actor);
                SCHED_DIAG("finishDrain RE-QUEUE (backlog) actor=" << actor);
            }
            return;
        }
        // Fully released. A suspended actor stays anchored in the live
        // registry (its awaited future's settle will reschedule it via
        // schedule(), which the 3-state flag turns into an enqueue); a
        // completed actor with an empty mailbox drops its anchor.
        if (!suspended) registryRemove(ctx, actor);
        SCHED_DIAG("finishDrain RELEASE actor=" << actor
                   << (suspended ? " (suspended)" : ""));
        return;
    }
}

size_t STRuntime::scheduledCount() const {
    // Read the lock-free ready queue's length. A diagnostic / test accessor;
    // a plain read of the ProtoList size off the (pinned) live registry.
    auto* ctx = impl_->rootCtx;
    if (!impl_->liveRegistry) return 0;
    const proto::ProtoObject* cur =
        impl_->liveRegistry->getOwnAttributeDirect(ctx, impl_->bootstrap.sym.ready);
    if (!cur || cur == PROTO_NONE) return 0;
    const proto::ProtoList* curList = cur->asList(ctx);
    return curList ? static_cast<size_t>(curList->getSize(ctx)) : 0;
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
    // F6 v3 E5: `fut` is a fresh mutable object held across three setAttribute
    // calls and installFutureCV (which allocates an ExternalPointer and does
    // another setAttribute). The sole caller (the engine actor SEND fast-path)
    // runs on a sized engine context, so the scratch region exists. Pin it.
    TransientPin pinFut(ctx, fut);
    const proto::ProtoString* stateKey =
        impl_->bootstrap.sym.state;
    const proto::ProtoString* valueKey =
        impl_->bootstrap.sym.value;
    const proto::ProtoString* errKey =
        impl_->bootstrap.sym.error;
    fut->setAttribute(ctx, stateKey, ctx->fromLong(0));  // 0 = pending
    fut->setAttribute(ctx, valueKey, PROTO_NONE);
    fut->setAttribute(ctx, errKey,   PROTO_NONE);
    // No per-future mutex / condition_variable. The future state machine is
    // lock-free: resolve/reject claim the settlement with an attribute CAS
    // (ProtoObject::setAttributeIfEqual), and Future>>wait polls __state__
    // with a GC-safe bounded sleep. See future_prims.cpp.
    return fut;
}

bool STRuntime::isActor(proto::ProtoContext* ctx,
                        const proto::ProtoObject* obj) const {
    if (!obj || obj == PROTO_NONE) return false;
    const proto::ProtoString* wrappedKey =
        impl_->bootstrap.sym.wrapped;
    auto* w = obj->getAttribute(ctx, wrappedKey);
    return (w != nullptr && w != PROTO_NONE);
}

// Track 4 (T4-a): Locate the standard-library `lib/` directory holding the
// stdlib `.st` modules (lib/stream.st, lib/math.st, ...).
//
// Discovery scheme (first hit wins):
//   1. $PROTOST_LIB — an explicit override pointing straight at a `lib/` dir.
//   2. Derived from the executable: read /proc/self/exe, then probe
//      <dir-of-exe>/lib, <dir-of-exe>/../lib, <dir-of-exe>/../../lib and the
//      installed layout <dir-of-exe>/../share/protoST/lib. A dev build runs
//      `build/protost`, so `../lib` resolves the project `lib/`; an installed
//      `bin/protost` finds `<prefix>/share/protoST/lib` (where CPack places
//      the stdlib `.st` modules).
//   3. <cwd>/lib — convenient when running from a project checkout.
// Returns "" if no `lib/` directory is found.
static std::string discoverStdlibDir() {
    namespace fs = std::filesystem;

    // 1. Explicit override.
    if (const char* env = std::getenv("PROTOST_LIB")) {
        if (env[0] != '\0') {
            std::error_code ec;
            fs::path p(env);
            if (fs::is_directory(p, ec)) return p.string();
        }
    }

    // 2. Derived from the executable location (/proc/self/exe on Linux).
    {
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec && !exe.empty()) {
            fs::path dir = exe.parent_path();
            const fs::path candidates[] = {
                dir / "lib",
                dir.parent_path() / "lib",
                dir.parent_path().parent_path() / "lib",
                // Installed layout: <prefix>/bin/protost ->
                // <prefix>/share/protoST/lib.
                dir.parent_path() / "share" / "protoST" / "lib",
            };
            for (const auto& c : candidates) {
                if (fs::is_directory(c, ec)) return c.string();
            }
        }
    }

    // 3. <cwd>/lib.
    {
        std::error_code ec;
        fs::path p = fs::current_path(ec) / "lib";
        if (!ec && fs::is_directory(p, ec)) return p.string();
    }

    return "";
}

// F5-M1: Resolve a logical module path to a filesystem path.
// Search order: cwd, $STPATH (colon-separated), active venv modules dir,
// and finally the standard-library `lib/` directory (T4-a). The user's cwd
// and $STPATH take precedence, so a user module can shadow a stdlib module.
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

    // 4. Standard-library `lib/` directory (T4-a). Lowest precedence — a
    // same-named module in the cwd, $STPATH or the venv shadows the stdlib.
    {
        std::string libDir = discoverStdlibDir();
        if (!libDir.empty()) {
            fs::path p = fs::path(libDir) / name;
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
    // F8-1: stamp the source file path so the debugger can map this
    // module (and its sub-blocks) back to a source file.
    bc->setSourceName(filePath);
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
    // F6 v3 E5: `moduleObj` is held across the class-binding loop, which
    // interns a fresh symbol and runs getAttribute + setAttribute per class.
    // Pin it. The prior runTopLevel run already sized `ctx`.
    TransientPin pinModuleObj(ctx, moduleObj);
    auto* g = globals();

    for (const auto& [className, info] : C.classes()) {
        (void)info;  // metadata; only the name is used here.
        if (className.empty() || className[0] == '_') continue;
        auto* classSym = proto::ProtoString::createSymbol(ctx, className.c_str());
        // F6 v3 E5: `classSym` is freshly interned and held across the
        // getAttribute + setAttribute below — pin it per iteration.
        TransientPin pinClassSym(
            ctx, reinterpret_cast<const proto::ProtoObject*>(classSym));
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

// T5-a: consumer-side cross-language interop.
//
// protoST's constructor sets the space's resolution chain to just
// `["provider:st"]` — its own UMD provider. protoCore's getImportModule walks
// that chain entry-by-entry; for a `provider:<key>` entry it looks the key up
// in the global ProviderRegistry. A foreign runtime's provider (protoJS,
// protoPython, or a test stand-in) may be *registered* with the registry, but
// it is never *reached* unless its spec is in this space's chain. This method
// closes that gap: it appends a foreign provider spec to the chain, leaving
// `provider:st` first so a same-named protoST module still shadows a foreign
// one. The filesystem fallback that getImportModule applies after the chain is
// unchanged.
void STRuntime::addModuleProviderToChain(const std::string& providerSpec) {
    if (providerSpec.empty()) return;
    auto* ctx = impl_->rootCtx;

    // The current chain is a ProtoList of ProtoString specs. Read it back,
    // scan for an existing identical entry (idempotency), and append if absent.
    const proto::ProtoObject* chainObj = impl_->space.getResolutionChain();
    const proto::ProtoList* chain =
        (chainObj && chainObj != PROTO_NONE) ? chainObj->asList(ctx) : nullptr;
    if (!chain) chain = ctx->newList();

    unsigned long n = chain->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* e = chain->getAt(ctx, static_cast<int>(i));
        const proto::ProtoString* es = e ? e->asString(ctx) : nullptr;
        if (es && es->toStdString(ctx) == providerSpec) {
            return;  // already present — nothing to do
        }
    }

    const proto::ProtoList* updated =
        chain->appendLast(ctx, ctx->fromUTF8String(providerSpec.c_str()));
    impl_->space.setResolutionChain(updated->asObject(ctx));
}

} // namespace protoST
