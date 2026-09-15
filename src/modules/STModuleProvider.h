#pragma once
#include "protoCore.h"
#include <string>

namespace protoST {

class STRuntime;

// F5 v2: ModuleProvider that exposes protoST source modules to protoCore's
// Universal Module Discovery (UMD). The provider is stateless — at load time
// it finds the STRuntime that owns the calling context's ProtoSpace and
// delegates to its findModuleFile / importModuleFile helpers.
class STModuleProvider : public proto::ModuleProvider {
public:
    STModuleProvider();
    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                       proto::ProtoContext* ctx) override;
    const std::string& getGUID() const override { return guid_; }
    const std::string& getAlias() const override { return alias_; }

private:
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

} // namespace protoST
