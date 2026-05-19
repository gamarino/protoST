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

private:
    STRuntime& rt_;
};

} // namespace protoST
