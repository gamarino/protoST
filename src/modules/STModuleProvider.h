#pragma once
#include "protoCore.h"
#include <string>

namespace protoST {

class STRuntime;

// F5 v2: ModuleProvider that exposes protoST source modules to protoCore's
// Universal Module Discovery (UMD). The provider is stateless — at load time
// it looks up the current STRuntime via a thread-local pointer and delegates
// to its findModuleFile / loadModuleFromFile helpers.
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

// Sets the runtime that the provider will dispatch to. Called by STRuntime
// ctor/dtor. Thread-local so multiple runtimes across threads don't clash;
// in single-thread use (the common case in protoST today) the last
// constructor wins.
void setCurrentSTRuntime(STRuntime* rt);
STRuntime* currentSTRuntime();

} // namespace protoST
