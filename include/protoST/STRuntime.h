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

namespace protoST { class DebuggerRuntime; }

namespace protoST {

class BytecodeModule;
class ExecutionEngine;
struct Bootstrap;
struct PrimitiveRegistry;

class STRuntime {
public:
    STRuntime();
    ~STRuntime();
    STRuntime(const STRuntime&) = delete;
    STRuntime& operator=(const STRuntime&) = delete;

    proto::ProtoSpace*   space()         const;
    proto::ProtoContext* rootCtx()       const;
    proto::ProtoRootSet* asyncRootSet()  const;

    // Mutable globals namespace (PUSH_GLOBAL / STORE_GLOBAL). Allocated at
    // STRuntime construction as a mutable child of objectProto, with "Object"
    // pre-registered so that `Object subclass: ...` compiles can resolve it.
    proto::ProtoObject*  globals()       const;

    // Access to the bootstrap prototype set (Object/Number/...).
    const Bootstrap& bootstrap() const;

    // Access to the primitive registry that backs SEND dispatch.
    PrimitiveRegistry& registry();

    // Access to the debugger runtime (attach state + session entry).
    DebuggerRuntime& debugger();

    // Convert a BytecodeModule constant pool entry to a ProtoObject (lazy materialisation).
    const proto::ProtoObject* materialize(const BytecodeModule& m, size_t constIdx) const;

    // Run a module against the runtime; returns the final value (top of stack at RETURN_TOP).
    const proto::ProtoObject* runTopLevel(const BytecodeModule& m);

    // F6 actor scheduler — single-thread MVP.
    // schedule() is idempotent for already-scheduled actors.
    void schedule(const proto::ProtoObject* actor);
    bool drainOne(proto::ProtoContext* ctx);   // returns true if a message was processed
    size_t scheduledCount() const;              // size of ready queue (for testing)

    // F6-A4 helpers
    // Allocates a new pending Future (mutable child of futureProto) with the
    // canonical attribute layout (__state__=0, __value__=nil, __error__=nil).
    const proto::ProtoObject* newFuture(proto::ProtoContext* ctx);
    // Cheap actor detection: an object is treated as an actor iff it carries
    // a non-nil __wrapped__ attribute (set by Object>>asActor).
    bool isActor(proto::ProtoContext* ctx, const proto::ProtoObject* obj) const;

    inline const char* versionTag() const { return "0.1.0-pre"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline const char* versionString() { return "protoST 0.1.0-pre"; }

} // namespace protoST
