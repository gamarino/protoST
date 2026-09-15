#include "STModuleProvider.h"
#include "protoST/STRuntime.h"
#include "runtime/NativeExceptionBridge.h"
#include "protoCore.h"

#include <algorithm>
#include <mutex>
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

STModuleProvider::STModuleProvider()
    : guid_("protoST-source-v1")
    , alias_("st")
{}

const proto::ProtoObject*
STModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    auto* rt = ctx ? stRuntimeForSpace(ctx->space) : nullptr;
    if (!rt) return PROTO_NONE;

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
