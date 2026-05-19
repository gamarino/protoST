#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Bootstrap.h"
#include "Venv.h"
#include "debugger/DebuggerRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
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

// F6-A6: future callback dispatch helpers defined alongside the Future
// primitives. drainOne uses these to fire thenDo:/catch: blocks at the moment
// the future transitions out of pending, without re-implementing the loop.
namespace protoST {
void fireFutureThenCallbacks(STRuntime& rt, proto::ProtoContext* ctx,
                              const proto::ProtoObject* future,
                              const proto::ProtoObject* value);
void fireFutureCatchCallbacks(STRuntime& rt, proto::ProtoContext* ctx,
                               const proto::ProtoObject* future,
                               const proto::ProtoObject* error);
}

namespace protoST {

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

STRuntime::STRuntime() : impl_(std::make_unique<Impl>()) {
    installIntPrimitives(*this);
    installBoolPrimitives(*this);
    installStringPrimitives(*this);
    installBlockPrimitives(*this);
    installDebuggerPrimitives(*this);
    installObjectPrimitives(*this);
    installFuturePrimitives(*this);
    installImportGlobal(*this);
}
STRuntime::~STRuntime() = default;

proto::ProtoSpace*   STRuntime::space()         const { return &impl_->space; }
proto::ProtoContext* STRuntime::rootCtx()       const { return impl_->rootCtx; }
proto::ProtoRootSet* STRuntime::asyncRootSet()  const { return impl_->asyncRoots; }
const Bootstrap&     STRuntime::bootstrap()     const { return impl_->bootstrap; }
PrimitiveRegistry&   STRuntime::registry()            { return impl_->registry; }
DebuggerRuntime&     STRuntime::debugger()            { return impl_->debugger; }
proto::ProtoObject*  STRuntime::globals()       const { return impl_->globals; }

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
    if (impl_->scheduledSet.insert(actor).second) {
        impl_->readyQueue.push(actor);
    }
}

bool STRuntime::drainOne(proto::ProtoContext* ctx) {
    if (impl_->readyQueue.empty()) return false;
    auto* actor = impl_->readyQueue.front();
    impl_->readyQueue.pop();
    impl_->scheduledSet.erase(actor);

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
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* valueKey =
        ctx->fromUTF8String("__value__")->asString(ctx);
    static const proto::ProtoString* errorKey =
        ctx->fromUTF8String("__error__")->asString(ctx);

    // Get the mailbox (ProtoList). Pop the first (FIFO).
    auto* mbObj = actor->getAttribute(ctx, mailboxKey);
    if (!mbObj || mbObj == PROTO_NONE) return true;
    auto* mailbox = mbObj->asList(ctx);
    if (!mailbox || mailbox->getSize(ctx) == 0) return true;

    auto* msg = mailbox->getAt(ctx, 0);
    // Drop the first message — getSlice(from, to) over [1, size).
    auto* remaining = mailbox->getSlice(
        ctx, 1, static_cast<int>(mailbox->getSize(ctx)));
    const_cast<proto::ProtoObject*>(actor)->setAttribute(
        ctx, mailboxKey, remaining->asObject(ctx));

    // Re-schedule actor if more messages remain.
    if (remaining->getSize(ctx) > 0) schedule(actor);

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
            const_cast<proto::ProtoObject*>(future)
                ->setAttribute(ctx, stateKey, ctx->fromLong(2));
            const_cast<proto::ProtoObject*>(future)
                ->setAttribute(ctx, errorKey, err);
            // F6-A6: fire any catch: callbacks registered while pending.
            fireFutureCatchCallbacks(*this, ctx, future, err);
        }
        return true;
    }

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
            const_cast<proto::ProtoObject*>(future)
                ->setAttribute(ctx, stateKey, ctx->fromLong(1));
            const_cast<proto::ProtoObject*>(future)
                ->setAttribute(ctx, valueKey, v);
            // F6-A6: fire any thenDo: callbacks registered while pending.
            fireFutureThenCallbacks(*this, ctx, future, v);
        }
    } catch (const std::exception& e) {
        if (future) {
            auto* err = ctx->fromUTF8String(e.what());
            const_cast<proto::ProtoObject*>(future)
                ->setAttribute(ctx, stateKey, ctx->fromLong(2));
            const_cast<proto::ProtoObject*>(future)
                ->setAttribute(ctx, errorKey, err);
            // F6-A6: fire any catch: callbacks registered while pending.
            fireFutureCatchCallbacks(*this, ctx, future, err);
        }
    }
    return true;
}

size_t STRuntime::scheduledCount() const {
    return impl_->readyQueue.size();
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
