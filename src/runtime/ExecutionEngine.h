#pragma once

namespace proto {
    class ProtoContext;
    class ProtoObject;
}

namespace protoST {

class STRuntime;
class BytecodeModule;

class ExecutionEngine {
public:
    explicit ExecutionEngine(STRuntime& rt) : rt_(rt) {}

    // Runs `m` in `ctx`; returns the value at RETURN_TOP (or method RETURN).
    const proto::ProtoObject* run(proto::ProtoContext* ctx,
                                  const BytecodeModule& m,
                                  const proto::ProtoObject* self = nullptr);

    // Runs `m` with `argc` arguments pre-loaded into locals 0..argc-1.
    // Used by BlockClosure>>value etc. (Task 44).
    const proto::ProtoObject* runWithArgs(proto::ProtoContext* ctx,
                                          const BytecodeModule& m,
                                          const proto::ProtoObject* self,
                                          const proto::ProtoObject* const* args,
                                          int argc);

private:
    STRuntime& rt_;
};

} // namespace protoST
