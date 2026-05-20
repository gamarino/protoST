#include "STModuleProvider.h"
#include "protoST/STRuntime.h"
#include "runtime/NativeExceptionBridge.h"
#include "protoCore.h"

namespace protoST {

namespace {
    thread_local STRuntime* g_currentRt = nullptr;
}

void setCurrentSTRuntime(STRuntime* rt) { g_currentRt = rt; }
STRuntime* currentSTRuntime() { return g_currentRt; }

STModuleProvider::STModuleProvider()
    : guid_("protoST-source-v1")
    , alias_("st")
{}

const proto::ProtoObject*
STModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    auto* rt = currentSTRuntime();
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
        auto* mod = rt->loadModuleFromFile(ctx, path, logicalPath);
        return mod ? mod : PROTO_NONE;
    });
}

} // namespace protoST
