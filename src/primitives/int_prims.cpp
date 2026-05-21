#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>

namespace protoST {

// Numeric-tower primitives (D11 / D20).
//
// These were once integer-only and computed with raw C `long long`
// (`asLong` / `fromLong`), which silently wrapped on overflow and could not
// touch a Float. They are now thin delegations to protoCore's own arithmetic
// on `ProtoObject` — `add`, `subtract`, `multiply`, `divide`, `modulo`,
// `compare`, `negate`, `abs`. protoCore is a full dynamic object system: its
// arithmetic already handles
//   * SmallInteger + LargeInteger + Float operands,
//   * mixed-mode coercion (`1 + 2.5` -> a Float),
//   * transparent promotion — an integer result that exceeds the 56-bit
//     inline `SmallInteger` range becomes a heap `LargeInteger` and stays
//     exact; a Float result becomes a heap `Double`.
//
// Because the protoST way is "minimal decoration over protoCore", the protoST
// primitives merely forward to it. They are bound on the shared `numberProto`
// (see installIntPrimitives), so `SmallInteger`, `LargeInteger` and `Float`
// all inherit one arithmetic protocol.
//
// Division note: protoCore's `/` is integer (truncating) division when both
// operands are integers, and float division when either operand is a Float.
// protoST has no `Fraction` type, so `/` follows protoCore exactly: `4 / 2`
// answers the integer `2`, `1 / 3` answers the integer `0`, and `1 / 2.0`
// answers the Float `0.5`. `//` is an explicit integer-division alias and
// `\\` is the modulo (remainder). This is documented in LANGUAGE.md §12.2.

namespace {

// Reject a non-numeric argument with a clear message instead of letting
// protoCore's arithmetic fault on it.
void requireNumber(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                   const char* who) {
    if (!isNumber(ctx, v)) {
        throw std::runtime_error(std::string(who) +
                                 ": argument is not a number");
    }
}

#define DEFBIN(NAME, METHOD, SELECTOR)                                            \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,        \
                                       const proto::ProtoObject* r,                \
                                       const proto::ProtoObject* const* a,         \
                                       int argc) {                                 \
    if (argc != 1) throw std::runtime_error(SELECTOR " expects 1 arg");            \
    requireNumber(ctx, a[0], SELECTOR);                                            \
    return r->METHOD(ctx, a[0]);                                                   \
}

DEFBIN(NumAdd, add,      "+")
DEFBIN(NumSub, subtract, "-")
DEFBIN(NumMul, multiply, "*")

// `/` and `//` both delegate to protoCore `divide`; `\\` to `modulo`.
const proto::ProtoObject* prim_NumDiv(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("/ expects 1 arg");
    requireNumber(ctx, a[0], "/");
    // ZeroDivide guard: an integer-zero or float-zero denominator.
    bool zero = a[0]->isFloat(ctx) ? (a[0]->asDouble(ctx) == 0.0)
                                   : (a[0]->asLong(ctx) == 0);
    if (zero) throw std::runtime_error("ZeroDivide");
    return r->divide(ctx, a[0]);
}

const proto::ProtoObject* prim_NumIntDiv(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("// expects 1 arg");
    requireNumber(ctx, a[0], "//");
    bool zero = a[0]->isFloat(ctx) ? (a[0]->asDouble(ctx) == 0.0)
                                   : (a[0]->asLong(ctx) == 0);
    if (zero) throw std::runtime_error("ZeroDivide");
    return r->divide(ctx, a[0]);
}

const proto::ProtoObject* prim_NumMod(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("\\\\ expects 1 arg");
    requireNumber(ctx, a[0], "\\\\");
    bool zero = a[0]->isFloat(ctx) ? (a[0]->asDouble(ctx) == 0.0)
                                   : (a[0]->asLong(ctx) == 0);
    if (zero) throw std::runtime_error("ZeroDivide");
    return r->modulo(ctx, a[0]);
}

#define DEFCMP(NAME, COND, SELECTOR)                                              \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,        \
                                       const proto::ProtoObject* r,                \
                                       const proto::ProtoObject* const* a, int) {   \
    requireNumber(ctx, a[0], SELECTOR);                                            \
    int c = r->compare(ctx, a[0]);                                                 \
    return (COND) ? PROTO_TRUE : PROTO_FALSE;                                       \
}
DEFCMP(NumLt, c <  0, "<")
DEFCMP(NumLe, c <= 0, "<=")
DEFCMP(NumGt, c >  0, ">")
DEFCMP(NumGe, c >= 0, ">=")

// `=` / `~=` — value equality across the whole numeric tower (so `2 = 2.0`).
// A non-numeric argument is simply unequal rather than an error, matching the
// catch-all `Object>>=` it overrides.
const proto::ProtoObject* prim_NumEq(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    if (!isNumber(ctx, a[0])) return PROTO_FALSE;
    return (r->compare(ctx, a[0]) == 0) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_NumNe(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    if (!isNumber(ctx, a[0])) return PROTO_TRUE;
    return (r->compare(ctx, a[0]) != 0) ? PROTO_TRUE : PROTO_FALSE;
}

// Unary operations — delegate to protoCore (these promote / coerce too).
const proto::ProtoObject* prim_NumNegated(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    return r->negate(ctx);
}
const proto::ProtoObject* prim_NumAbs(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const*, int) {
    return r->abs(ctx);
}

// `printString` for the whole numeric tower. protoCore does not render a
// number to a string, so protoST formats it (see ValueFormat::formatNumber):
// a Float shows a fractional part, a LargeInteger shows its exact digits.
const proto::ProtoObject* prim_NumPrintString(STRuntime&, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    return ctx->fromUTF8String(formatNumber(ctx, r).c_str());
}

// `isEven` / `isOdd` — integer parity. A Float is neither (a parity send to a
// non-integral Float is a meaningless question; we answer false for both).
const proto::ProtoObject* prim_NumIsEven(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) return PROTO_FALSE;
    const proto::ProtoObject* rem = r->modulo(ctx, ctx->fromLong(2));
    return (rem->asLong(ctx) == 0) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_NumIsOdd(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) return PROTO_FALSE;
    const proto::ProtoObject* rem = r->modulo(ctx, ctx->fromLong(2));
    return (rem->asLong(ctx) != 0) ? PROTO_TRUE : PROTO_FALSE;
}

} // anon

void installIntPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    // Bound on the shared `numberProto` so SmallInteger, LargeInteger and
    // Float all inherit one numeric protocol (D11 / D20).
    auto* N = b.numberProto;
    bindPrimitive(rt, N, "+",  reg.registerPrim(prim_NumAdd));
    bindPrimitive(rt, N, "-",  reg.registerPrim(prim_NumSub));
    bindPrimitive(rt, N, "*",  reg.registerPrim(prim_NumMul));
    bindPrimitive(rt, N, "/",  reg.registerPrim(prim_NumDiv));
    bindPrimitive(rt, N, "//", reg.registerPrim(prim_NumIntDiv));
    bindPrimitive(rt, N, "\\\\", reg.registerPrim(prim_NumMod));
    bindPrimitive(rt, N, "<",  reg.registerPrim(prim_NumLt));
    bindPrimitive(rt, N, "<=", reg.registerPrim(prim_NumLe));
    bindPrimitive(rt, N, ">",  reg.registerPrim(prim_NumGt));
    bindPrimitive(rt, N, ">=", reg.registerPrim(prim_NumGe));
    bindPrimitive(rt, N, "=",  reg.registerPrim(prim_NumEq));
    bindPrimitive(rt, N, "~=", reg.registerPrim(prim_NumNe));
    bindPrimitive(rt, N, "negated", reg.registerPrim(prim_NumNegated));
    bindPrimitive(rt, N, "abs",     reg.registerPrim(prim_NumAbs));
    bindPrimitive(rt, N, "isEven",  reg.registerPrim(prim_NumIsEven));
    bindPrimitive(rt, N, "isOdd",   reg.registerPrim(prim_NumIsOdd));
    bindPrimitive(rt, N, "printString", reg.registerPrim(prim_NumPrintString));
}

} // namespace protoST
