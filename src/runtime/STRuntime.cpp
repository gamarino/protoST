#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Bootstrap.h"
#include "protoCore.h"

#include <cstring>
#include <new>
#include <type_traits>
#include <vector>

namespace protoST { void installIntPrimitives(STRuntime& rt); }
namespace protoST { void installBoolPrimitives(STRuntime& rt); }
namespace protoST { void installStringPrimitives(STRuntime& rt); }
namespace protoST { void installBlockPrimitives(STRuntime& rt); }

namespace protoST {

struct PrimitiveRegistry::Impl { std::vector<PrimFn> fns; };
PrimitiveRegistry::PrimitiveRegistry() : impl(std::make_unique<Impl>()) {}
PrimitiveRegistry::~PrimitiveRegistry() = default;
int PrimitiveRegistry::registerPrim(PrimFn fn) {
    impl->fns.push_back(fn);
    return static_cast<int>(impl->fns.size()) - 1;
}
PrimFn PrimitiveRegistry::at(int i) const { return impl->fns.at(i); }
size_t PrimitiveRegistry::size() const   { return impl->fns.size(); }

void bindPrimitive(STRuntime& rt, const proto::ProtoObject* proto, const char* selector, int idx) {
    auto* ctx = rt.rootCtx();
    auto* selStr = ctx->fromUTF8String(selector);                  // create ProtoString
    auto* sel = selStr->asString(ctx);                              // intern as symbol
    // Tag bit 62 marks "this is a primitive marker, not a real method object".
    auto* val = ctx->fromLong(static_cast<long long>(idx) | (1LL << 62));
    const_cast<proto::ProtoObject*>(proto)->setAttribute(ctx, sel, val);
}

// protoCore's ProtoSpace constructor does not zero-initialise its
// `rootContext` member before the inner ProtoContext constructor reads it
// (via the main-thread auto-detect path).  When the raw bytes happen to
// look like a valid pointer, the dereference segfaults.  Workaround: zero
// the ProtoSpace storage with placement-new before invoking the ctor so
// the auto-detect short-circuits safely on nullptr.
struct STRuntime::Impl {
    alignas(proto::ProtoSpace) unsigned char spaceStorage[sizeof(proto::ProtoSpace)];
    proto::ProtoSpace*   spacePtr   = nullptr;
    proto::ProtoContext* rootCtx    = nullptr;
    proto::ProtoRootSet* asyncRoots = nullptr;
    Bootstrap            bootstrap;
    PrimitiveRegistry    registry;

    Impl() {
        std::memset(spaceStorage, 0, sizeof(spaceStorage));
        spacePtr = new (spaceStorage) proto::ProtoSpace();
        // protoCore exposes the root context as a public field on ProtoSpace
        // (see protoCore/headers/protoCore.h:1234 and protoJS/src/JSContext.cpp:100).
        rootCtx    = spacePtr->rootContext;
        asyncRoots = spacePtr->createRootSet("protoST-async");
        bootstrapPrototypes(*spacePtr, rootCtx, bootstrap);
    }

    ~Impl() {
        if (asyncRoots) {
            spacePtr->destroyRootSet(asyncRoots);
            asyncRoots = nullptr;
        }
        if (spacePtr) {
            spacePtr->~ProtoSpace();
            spacePtr = nullptr;
        }
    }
};

STRuntime::STRuntime() : impl_(std::make_unique<Impl>()) {
    installIntPrimitives(*this);
    installBoolPrimitives(*this);
    installStringPrimitives(*this);
    installBlockPrimitives(*this);
}
STRuntime::~STRuntime() = default;

proto::ProtoSpace*   STRuntime::space()         const { return impl_->spacePtr; }
proto::ProtoContext* STRuntime::rootCtx()       const { return impl_->rootCtx; }
proto::ProtoRootSet* STRuntime::asyncRootSet()  const { return impl_->asyncRoots; }
const Bootstrap&     STRuntime::bootstrap()     const { return impl_->bootstrap; }
PrimitiveRegistry&   STRuntime::registry()            { return impl_->registry; }

const proto::ProtoObject*
STRuntime::materialize(const BytecodeModule& m, size_t i) const {
    using K = BytecodeModule::ConstKind;
    auto* ctx = impl_->rootCtx;
    switch (m.constKind(i)) {
        case K::Integer:
            return ctx->fromLong(m.constInteger(i));
        case K::Float:
            return ctx->fromDouble(m.constFloat(i));
        case K::String:
            return ctx->fromUTF8String(m.constString(i).c_str());
        case K::Symbol: {
            const proto::ProtoObject* s =
                ctx->fromUTF8String(m.constSymbol(i).c_str());
            // ProtoObject::asString returns a ProtoString view of the value.
            return reinterpret_cast<const proto::ProtoObject*>(s->asString(ctx));
        }
        case K::Char:
            // F2 simplification: treat character literal as a 1-char string.
            return ctx->fromUTF8String(m.constString(i).c_str());
        case K::BlockRef:
            // F2 stub: block materialisation lands in a later task.
            return PROTO_NONE;
        case K::NilK:   return PROTO_NONE;
        case K::TrueK:  return PROTO_TRUE;
        case K::FalseK: return PROTO_FALSE;
    }
    return PROTO_NONE;
}

const proto::ProtoObject*
STRuntime::runTopLevel(const BytecodeModule& m) {
    ExecutionEngine eng(*this);
    return eng.run(impl_->rootCtx, m, /*self=*/PROTO_NONE);
}

} // namespace protoST
