#include "STModuleProvider.h"
#include "protoST/STRuntime.h"
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
    try {
        std::string path = rt->findModuleFile(logicalPath);
        if (path.empty()) return PROTO_NONE;
        auto* mod = rt->loadModuleFromFile(ctx, path, logicalPath);
        return mod ? mod : PROTO_NONE;
    } catch (...) {
        // Per ModuleProvider contract: no exceptions, return PROTO_NONE on failure.
        return PROTO_NONE;
    }
}

} // namespace protoST
