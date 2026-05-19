#pragma once
#include <cstddef>
#include <memory>

namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;

using PrimFn = const proto::ProtoObject* (*)(
    STRuntime&,
    proto::ProtoContext*,
    const proto::ProtoObject* receiver,
    const proto::ProtoObject* const* args,
    int argc);

struct PrimitiveRegistry {
    int  registerPrim(PrimFn fn);
    PrimFn at(int index) const;
    size_t size() const;

    PrimitiveRegistry();
    ~PrimitiveRegistry();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Bind a primitive on a prototype under the given selector. Stores primIndex as a tagged SmallInteger.
void bindPrimitive(STRuntime& rt,
                   const proto::ProtoObject* proto,
                   const char* selector,
                   int primIndex);

} // namespace protoST
