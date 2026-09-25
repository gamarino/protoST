#include "STModuleProvider.h"
#include "protoST/STRuntime.h"
#include "runtime/NativeExceptionBridge.h"
#include "protoCore.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace protoST {

namespace {
    struct RuntimeEntry {
        const proto::ProtoSpace* space;
        STRuntime* runtime;
    };

    // Intentionally leaked: an STRuntime may be destroyed during static
    // destruction, after a function-local static container would be gone.
    std::mutex& registryMutex() {
        static auto* m = new std::mutex();
        return *m;
    }
    std::vector<RuntimeEntry>& registryEntries() {
        static auto* v = new std::vector<RuntimeEntry>();
        return *v;
    }
}

void registerSTRuntime(const proto::ProtoSpace* space, STRuntime* rt) {
    if (!space || !rt) return;
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& entries = registryEntries();
    for (auto& e : entries) {
        if (e.space == space) { e.runtime = rt; return; }
    }
    entries.push_back({space, rt});
}

void unregisterSTRuntime(const proto::ProtoSpace* space, STRuntime* rt) {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& entries = registryEntries();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const RuntimeEntry& e) {
                                     return e.space == space && e.runtime == rt;
                                 }),
                  entries.end());
}

STRuntime* stRuntimeForSpace(const proto::ProtoSpace* space) {
    if (!space) return nullptr;
    std::lock_guard<std::mutex> lock(registryMutex());
    for (const auto& e : registryEntries()) {
        if (e.space == space) return e.runtime;
    }
    return nullptr;
}

std::size_t registeredSTRuntimeCount() {
    std::lock_guard<std::mutex> lock(registryMutex());
    return registryEntries().size();
}

STRuntime* soleSTRuntime() {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& entries = registryEntries();
    if (entries.size() != 1) return nullptr;
    return entries.front().runtime;
}

STModuleProvider::STModuleProvider()
    : guid_("protoST-source-v1")
    , alias_("st")
{}

namespace {

// Track Y: one exported binding of a protoST module, carried out of the walk
// over the module object's own attributes as plain C++ data.
//
// The VALUE is the protoST object itself and is never copied — it is only
// re-keyed. It stays reachable throughout because `importModuleFile` has
// already anchored the module with `registryAdd`, which puts it in
// `liveRegistry` — a mutable object pinned once in the `ProtoRootSet` this
// runtime creates on its own `ProtoSpace`, so it is a GC root for the life of
// the runtime — and the classes it names are also in `globals`.
//
// `Impl::moduleCache` is NOT a root: it is a `std::map` of raw pointers, so it
// gives a module its identity (one load per canonical path) but contributes no
// retention. The retention is `liveRegistry` and `globals`, both per-runtime.
// The platform rule is that a module is a process-level entity anchored from a
// global perennial list; this is that rule implemented per runtime. See R5 in
// protoScala's docs/STATUS.md for where the two differ.
struct Binding {
    std::string name;
    const proto::ProtoObject* value;
};

void collectBinding(proto::ProtoContext* stCtx, void* self,
                    const proto::ProtoString* name,
                    const proto::ProtoObject* value) {
    auto* out = static_cast<std::vector<Binding>*>(self);
    out->push_back(Binding{name->toStdString(stCtx), value});
}

// Track Y: the module namespace, rebuilt so the CALLER can read it.
//
// WHY THIS IS STILL NEEDED AFTER protoCore 2.2.0 (P3).
//
// The original reason was interning. An attribute key is the ADDRESS of an
// interned symbol, and protoCore used to intern per ProtoSpace
// (`ctx->space->symbolTable`), so a name the caller interned in its own space
// was a DIFFERENT pointer from the one protoST stored the binding under —
// unless the name was short enough for protoCore to embed it in the pointer
// word, in which case the two agreed by accident. That accident was the trap:
// `value` (5 bytes) resolved and `Counter` (7 bytes) silently missed, because
// getAttribute returned PROTO_NONE, which is also a legitimate value.
//
// protoCore 2.2.0 fixed THAT half: interning is process-global, so
// createSymbol returns one canonical pointer per spelling per process and the
// key rebuild below is now redundant as a key rebuild.
//
// The facade survives because it does a SECOND thing that P3 did not fix: it
// re-parents the namespace object to `callerCtx->space->objectPrototype`, and
// PROTOTYPES REMAIN PER SPACE. A protoST object handed straight to another
// runtime still carries parent links into protoST's prototype chain. Global
// interning fixes names, not prototypes; deleting the facade would trade a
// silent attribute miss for a silent prototype mismatch (P3 D9).
//
// So the namespace mapping — and only the mapping — is rebuilt, in the
// caller's space and against the caller's prototype. Every VALUE is the
// protoST object itself, by address: no copy, no serialisation, no proxy
// object per member. This is what `ctx` is for in `tryLoad(path, ctx)`:
// allocating the result in the caller's context.
const proto::ProtoObject* buildCallerFacade(proto::ProtoContext* callerCtx,
                                            const std::vector<Binding>& bindings) {
    // The facade and every symbol are held in C++ locals across allocations
    // that only get attached to a GC root at the end — protoCore's own
    // ModuleResolver.cpp wraps exactly this shape in a CriticalSection.
    proto::ProtoContext::CriticalSection cs(callerCtx);
    const proto::ProtoObject* facade = callerCtx->newObject(false);
    if (!facade) return PROTO_NONE;
    if (callerCtx->space->objectPrototype) {
        facade = facade->addParent(callerCtx, callerCtx->space->objectPrototype);
        if (!facade) return PROTO_NONE;
    }
    for (const Binding& b : bindings) {
        const proto::ProtoString* key =
            proto::ProtoString::createSymbol(callerCtx, b.name.c_str());
        if (!key) continue;
        facade = facade->setAttribute(callerCtx, key,
                                     b.value ? b.value : PROTO_NONE);
        if (!facade) return PROTO_NONE;
    }
    return facade;
}

}  // namespace

// Track Y: a caller in another runtime's ProtoSpace.
//
// The load runs on protoST's OWN root context: protoST's prototypes, globals
// and interned symbols all live in protoST's space, and running a module's top
// level on a context in the caller's space would allocate protoST objects
// there, intern the module's literals in the caller's symbol table, and leave
// protoST's own later lookups of those names missing (the same per-space
// interning trap T5-a hit in `prim_Import_from`).
//
// `rootCtx` may only be allocated on by the thread that constructed the
// runtime (D26), and a foreign caller has no context of its own in this space
// to chain to, so an import from any other thread is refused with a message
// rather than raced.
const proto::ProtoObject*
STModuleProvider::loadForForeignCaller(STRuntime& rt, const std::string& logicalPath,
                                       proto::ProtoContext* callerCtx) {
    if (!rt.isOwnerThread()) {
        throw std::runtime_error(
            "protoST: a cross-runtime import of '" + logicalPath +
            "' must run on the thread that constructed the protoST runtime");
    }

    // "Not my module": a plain miss, before anything else happens.
    const std::string path = rt.findModuleFile(logicalPath);
    if (path.empty()) return PROTO_NONE;

    std::vector<Binding> bindings;
    try {
        proto::ProtoContext* stCtx = rt.rootCtx();
        const proto::ProtoObject* mod = rt.importModuleFile(stCtx, path, logicalPath);
        if (!mod || mod == PROTO_NONE) return PROTO_NONE;
        mod->processOwnAttributes(stCtx, &bindings, collectBinding);
    } catch (const std::exception&) {
        // A parse error, a compile error, an import cycle, or an uncaught
        // protoST `Error` (UnhandledSTException, itself a std::runtime_error).
        // All carry a message; the importing runtime's own boundary translates
        // them into its own exception type.
        throw;
    } catch (...) {
        // protoST's control-flow unwinds (NonLocalReturn, UnwindToHandler,
        // RetrySignal, ResumeSignal, PassSignal, FutureYield) are not
        // std::exceptions and mean "resume protoST execution somewhere up the
        // protoST stack" — there is no such frame on a foreign importer's
        // stack, and letting one cross would unwind into a runtime that cannot
        // interpret it. Report it instead.
        throw std::runtime_error(
            "protoST: the top level of module '" + logicalPath +
            "' left through a control-flow unwind (a non-local return, a "
            "resumption, a retry, or a Future yield), which cannot cross a "
            "runtime boundary");
    }

    // Allocated in the CALLER's context, which is what `ctx` is for.
    return buildCallerFacade(callerCtx, bindings);
}

const proto::ProtoObject*
STModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    if (!ctx || !ctx->space) return PROTO_NONE;

    auto* rt = stRuntimeForSpace(ctx->space);
    if (!rt) {
        // Track Y: the caller's ProtoSpace is not one protoST owns, so this is
        // a cross-runtime import and the runtime comes from the provider's own
        // state, never from `ctx->space` — resolving it from the caller's space
        // is exactly the Phase 6 defect, which reported "no module" for a
        // module that was there.
        STRuntime* own = soleSTRuntime();
        if (!own) {
            if (registeredSTRuntimeCount() > 1) {
                throw std::runtime_error(
                    "protoST: a cross-runtime import of '" + logicalPath +
                    "' is ambiguous — this process holds more than one protoST "
                    "runtime and the UMD provider is registered once per "
                    "process, so there is no way to tell which one is meant");
            }
            return PROTO_NONE;  // no protoST runtime in this process
        }
        return loadForForeignCaller(*own, logicalPath, ctx);
    }

    // "Not my module" — no file resolves to this logical path. Per the
    // protoCore ModuleProvider contract this is a plain miss: return
    // PROTO_NONE so UMD moves on to the next provider. NOT an error.
    std::string path = rt->findModuleFile(logicalPath);
    if (path.empty()) return PROTO_NONE;

    // EXC-d: the module file exists but its load (read / parse / compile /
    // top-level run) may throw a C++ exception. This is a native call
    // boundary — route it through translateNativeException so a genuine
    // failure becomes a catchable protoST `Error` (carrying the real failure
    // message, not a generic "not found"). If an `on: Error do:` is active
    // the wrapper's signal path catches it and unwinds via UnwindToHandler;
    // with no handler it raises UnhandledSTException. Both are non-
    // std::exception-or-runtime_error control flow that propagate cleanly
    // back through UMD to the importing `prim_Import_from` call site. The
    // control-flow siblings (NonLocalReturn etc.) are re-thrown untouched.
    return translateNativeException(*rt, ctx, [&]() -> const proto::ProtoObject* {
        // S6: importModuleFile runs the module's top level exactly once even
        // when several threads import it at the same moment.
        auto* mod = rt->importModuleFile(ctx, path, logicalPath);
        return mod ? mod : PROTO_NONE;
    });
}

} // namespace protoST
