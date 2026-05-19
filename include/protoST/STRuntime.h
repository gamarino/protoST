#pragma once
#include <cstddef>
#include <memory>

// Forward declarations to avoid dragging protoCore headers into the public include.
namespace proto {
    class ProtoSpace;
    class ProtoContext;
    class ProtoObject;
    class ProtoRootSet;
    class ProtoString;
}

namespace protoST {

class BytecodeModule;
class ExecutionEngine;

class STRuntime {
public:
    STRuntime();
    ~STRuntime();
    STRuntime(const STRuntime&) = delete;
    STRuntime& operator=(const STRuntime&) = delete;

    proto::ProtoSpace*   space()         const;
    proto::ProtoContext* rootCtx()       const;
    proto::ProtoRootSet* asyncRootSet()  const;

    // Convert a BytecodeModule constant pool entry to a ProtoObject (lazy materialisation).
    const proto::ProtoObject* materialize(const BytecodeModule& m, size_t constIdx) const;

    // Run a module against the runtime; returns the final value (top of stack at RETURN_TOP).
    const proto::ProtoObject* runTopLevel(const BytecodeModule& m);

    inline const char* versionTag() const { return "0.1.0-pre"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline const char* versionString() { return "protoST 0.1.0-pre"; }

} // namespace protoST
