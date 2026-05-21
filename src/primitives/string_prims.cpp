#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace protoST {

namespace {

// protoCore exposes UTF-8 conversion via ProtoObject::asString(ctx)->toStdString(ctx).
// There is no direct ProtoObject::asUTF8String; the plan text was off, but the
// reachable API is equivalent and idiomatic across protoJS/protoPython.
static inline std::string toUtf8(const proto::ProtoObject* o, proto::ProtoContext* ctx) {
    return o->asString(ctx)->toStdString(ctx);
}

const proto::ProtoObject* prim_StrConcat(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int) {
    std::string out = toUtf8(r, ctx) + toUtf8(a[0], ctx);
    return ctx->fromUTF8String(out.c_str());
}

const proto::ProtoObject* prim_StrSize(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    return ctx->fromLong(static_cast<long long>(toUtf8(r, ctx).size()));
}

const proto::ProtoObject* prim_StrEq(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    return (toUtf8(r, ctx) == toUtf8(a[0], ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

// D18: `~=` on String — value-inequality. Object's default `~=` is
// identity-negation, which would wrongly report two distinct equal-valued
// String objects as unequal; String overrides it to compare contents.
const proto::ProtoObject* prim_StrNe(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    return (toUtf8(r, ctx) != toUtf8(a[0], ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_PrintNl(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    // `formatValue` renders strings, the whole numeric tower (SmallInteger /
    // LargeInteger / Float) and the default object form. protoCore's
    // `asString` answers nil for a number, so a bare `asString` here would
    // fault on `2 printNl` / `25 factorial printNl`.
    std::string s = formatValue(rt, ctx, r);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    return r;
}

} // anon

void installStringPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.stringProto, ",",       reg.registerPrim(prim_StrConcat));
    bindPrimitive(rt, b.stringProto, "size",    reg.registerPrim(prim_StrSize));
    bindPrimitive(rt, b.stringProto, "=",       reg.registerPrim(prim_StrEq));
    bindPrimitive(rt, b.stringProto, "~=",      reg.registerPrim(prim_StrNe));
    bindPrimitive(rt, b.stringProto, "printNl", reg.registerPrim(prim_PrintNl));
    bindPrimitive(rt, b.objectProto, "printNl", reg.registerPrim(prim_PrintNl)); // fallback
}

} // namespace protoST
