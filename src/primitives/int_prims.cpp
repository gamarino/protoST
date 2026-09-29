#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "runtime/ZeroDivideSignal.h"
#include "protoCore.h"

#include <cmath>
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

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

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

// Double dispatch for a number that is not native (a Fraction, a Point used as
// a number): the argument is asked to adapt the native receiver and perform
// the operation (Pharo's adaptToNumber:andSend:). Answers nullptr when the
// argument does not understand it, so the caller reports "not a number".
const proto::ProtoObject* adaptArgument(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* arg,
                                        const char* selector) {
    if (!arg || arg == PROTO_NONE || arg == PROTO_TRUE || arg == PROTO_FALSE
        || arg->asString(ctx))
        return nullptr;
    bool understood = false;
    const proto::ProtoObject* args2[2] = {
        r, reinterpret_cast<const proto::ProtoObject*>(proto::ProtoString::createSymbol(ctx, selector)) };
    const proto::ProtoObject* res = sendDynamic(
        rt, ctx, arg, proto::ProtoString::createSymbol(ctx, "adaptToNumber:andSend:"),
        args2, 2, &understood);
    return understood ? (res ? res : PROTO_NONE) : nullptr;
}

#define DEFBIN(NAME, METHOD, SELECTOR)                                            \
const proto::ProtoObject* prim_##NAME(STRuntime& rt, proto::ProtoContext* ctx,     \
                                       const proto::ProtoObject* r,                \
                                       const proto::ProtoObject* const* a,         \
                                       int argc) {                                 \
    if (argc != 1) throw std::runtime_error(SELECTOR " expects 1 arg");            \
    if (!isNumber(ctx, a[0]))                                                      \
        if (const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], SELECTOR)) \
            return adapted;                                                        \
    requireNumber(ctx, a[0], SELECTOR);                                            \
    return r->METHOD(ctx, a[0]);                                                   \
}

DEFBIN(NumAdd, add,      "+")
DEFBIN(NumSub, subtract, "-")
DEFBIN(NumMul, multiply, "*")

// Zero test that works across the numeric tower: asLong on a LargeInteger
// throws ("exceeds long long range"), which made every division by a
// LargeInteger fail before protoCore was even asked.
static bool isZeroNumber(proto::ProtoContext* ctx, const proto::ProtoObject* n) {
    if (n->isFloat(ctx)) return n->asDouble(ctx) == 0.0;
    return n->compare(ctx, ctx->fromInteger(0)) == 0;   // exact across the tower
}

static int signOfNumber(proto::ProtoContext* ctx, const proto::ProtoObject* n) {
    const int c = n->compare(ctx, ctx->fromInteger(0));
    return (c > 0) - (c < 0);
}

static void checkDivisor(proto::ProtoContext* ctx, const proto::ProtoObject* d, const char* sel) {
    requireNumber(ctx, d, sel);
    if (isZeroNumber(ctx, d)) throw ZeroDivideSignal();
}

// `/` delegates to protoCore `divide`.
const proto::ProtoObject* prim_NumDiv(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("/ expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], "/"))
            return adapted;
    checkDivisor(ctx, a[0], "/");
    return r->divide(ctx, a[0]);
}

// Smalltalk-80 floor division: `//` rounds the quotient toward negative
// infinity and `\\` answers the remainder with the sign of the divisor, so that
// (a // b) * b + (a \\ b) = a. protoCore's divide/modulo truncate toward zero
// (quo:/rem:); the floor forms correct them when the remainder and the divisor
// have opposite signs. A Float operand floors the real quotient.
static void floorDivMod(proto::ProtoContext* ctx, const proto::ProtoObject* r,
                        const proto::ProtoObject* d,
                        const proto::ProtoObject** quot, const proto::ProtoObject** rem) {
    if (r->isFloat(ctx) || d->isFloat(ctx)) {
        const double x = r->asDouble(ctx), y = d->asDouble(ctx);
        const double q = std::floor(x / y);
        if (quot) *quot = (std::fabs(q) < 9.2e18) ? ctx->fromLong(static_cast<long long>(q))
                                                  : ctx->fromDouble(q);
        if (rem) *rem = ctx->fromDouble(x - q * y);
        return;
    }
    const proto::ProtoObject* q = r->divide(ctx, d);
    const proto::ProtoObject* m = r->modulo(ctx, d);
    if (!isZeroNumber(ctx, m) && signOfNumber(ctx, m) != signOfNumber(ctx, d)) {
        q = q->subtract(ctx, ctx->fromInteger(1));
        m = m->add(ctx, d);
    }
    if (quot) *quot = q;
    if (rem) *rem = m;
}

const proto::ProtoObject* prim_NumIntDiv(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("// expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], "//"))
            return adapted;
    checkDivisor(ctx, a[0], "//");
    const proto::ProtoObject* q = nullptr;
    floorDivMod(ctx, r, a[0], &q, nullptr);
    return q;
}

const proto::ProtoObject* prim_NumMod(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("\\\\ expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], "\\\\"))
            return adapted;
    checkDivisor(ctx, a[0], "\\\\");
    const proto::ProtoObject* m = nullptr;
    floorDivMod(ctx, r, a[0], nullptr, &m);
    return m;
}

// quo: / rem: truncate toward zero (protoCore's divide / modulo on integers).
const proto::ProtoObject* prim_NumQuo(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("quo: expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], "quo:"))
            return adapted;
    checkDivisor(ctx, a[0], "quo:");
    if (r->isFloat(ctx) || a[0]->isFloat(ctx))
        return ctx->fromDouble(std::trunc(r->asDouble(ctx) / a[0]->asDouble(ctx)));
    return r->divide(ctx, a[0]);
}

const proto::ProtoObject* prim_NumRem(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("rem: expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], "rem:"))
            return adapted;
    checkDivisor(ctx, a[0], "rem:");
    if (r->isFloat(ctx) || a[0]->isFloat(ctx))
        return ctx->fromDouble(std::fmod(r->asDouble(ctx), a[0]->asDouble(ctx)));
    return r->modulo(ctx, a[0]);
}

// Ordered comparison through protoCore's partialCompare: exact by value across
// SmallInteger, LargeInteger and Float, and IEEE 754 for NaN — a NaN is
// unordered with every number (itself included), so every ordering is false.
// protoCore's `compare` is a total order that reports a NaN pair as equal; it
// must not implement these operators.
#define DEFCMP(NAME, COND, SELECTOR)                                              \
const proto::ProtoObject* prim_##NAME(STRuntime& rt, proto::ProtoContext* ctx,     \
                                       const proto::ProtoObject* r,                \
                                       const proto::ProtoObject* const* a, int) {   \
    if (!isNumber(ctx, a[0]))                                                      \
        if (const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], SELECTOR)) \
            return adapted;                                                        \
    requireNumber(ctx, a[0], SELECTOR);                                            \
    const std::partial_ordering c = r->partialCompare(ctx, a[0]);                  \
    return (COND) ? PROTO_TRUE : PROTO_FALSE;                                       \
}
DEFCMP(NumLt, c <  0, "<")
DEFCMP(NumLe, c <= 0, "<=")
DEFCMP(NumGt, c >  0, ">")
DEFCMP(NumGe, c >= 0, ">=")

// `=` / `~=` — value equality across the whole numeric tower (so `2 = 2.0`).
// A non-numeric argument is simply unequal rather than an error, matching the
// catch-all `Object>>=` it overrides. IEEE 754: a NaN is equal to nothing,
// itself included (`Float nan = Float nan` is false, `~=` true).
const proto::ProtoObject* prim_NumEq(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    if (!isNumber(ctx, a[0])) {
        const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], "=");
        return adapted == PROTO_TRUE ? PROTO_TRUE : PROTO_FALSE;
    }
    return (r->partialCompare(ctx, a[0]) == 0) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_NumNe(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    if (!isNumber(ctx, a[0])) {
        const proto::ProtoObject* adapted = adaptArgument(rt, ctx, r, a[0], "=");
        return adapted == PROTO_TRUE ? PROTO_FALSE : PROTO_TRUE;
    }
    return (r->partialCompare(ctx, a[0]) != 0) ? PROTO_TRUE : PROTO_FALSE;
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
const proto::ProtoObject* prim_NumPrintString(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    // A number class (SmallInteger, Float, ...) prints as its name.
    if (!r->isInteger(ctx) && !r->isFloat(ctx)) {
        const proto::ProtoObject* own =
            r->getOwnAttributeDirect(ctx, rt.bootstrap().sym.className);
        if (own && own != PROTO_NONE) return own;
    }
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


// Bitwise protocol on integers (two's complement, across the SmallInteger /
// LargeInteger tower as protoCore implements it).
void requireInteger(proto::ProtoContext* ctx, const proto::ProtoObject* v, const char* who) {
    if (!v || !v->isInteger(ctx))
        throw std::runtime_error(std::string(who) + ": receiver and argument must be integers");
}

#define DEFBITS(NAME, METHOD, SELECTOR)                                           \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,        \
                                       const proto::ProtoObject* r,                \
                                       const proto::ProtoObject* const* a,         \
                                       int argc) {                                 \
    if (argc != 1) throw std::runtime_error(SELECTOR " expects 1 arg");            \
    requireInteger(ctx, r, SELECTOR);                                              \
    requireInteger(ctx, a[0], SELECTOR);                                           \
    return r->METHOD(ctx, a[0]);                                                   \
}

DEFBITS(IntBitAnd, bitwiseAnd, "bitAnd:")
DEFBITS(IntBitOr,  bitwiseOr,  "bitOr:")
DEFBITS(IntBitXor, bitwiseXor, "bitXor:")

const proto::ProtoObject* prim_IntBitInvert(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    requireInteger(ctx, r, "bitInvert");
    return r->bitwiseNot(ctx);
}

// bitShift: n shifts left for positive n and right (arithmetic) for negative.
const proto::ProtoObject* prim_IntBitShift(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("bitShift: expects 1 arg");
    requireInteger(ctx, r, "bitShift:");
    requireInteger(ctx, a[0], "bitShift:");
    const long long n = a[0]->asLong(ctx);
    if (n > 1000000 || n < -1000000) throw std::runtime_error("bitShift: shift amount out of range");
    return n >= 0 ? r->shiftLeft(ctx, static_cast<int>(n))
                  : r->shiftRight(ctx, static_cast<int>(-n));
}
} // anon

// Exported for math_prims.cpp: see adaptArgument.
const proto::ProtoObject* adaptNumberArgument(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* arg,
                                              const char* selector) {
    return adaptArgument(rt, ctx, r, arg, selector);
}

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
    bindPrimitive(rt, N, "quo:", reg.registerPrim(prim_NumQuo));
    bindPrimitive(rt, N, "rem:", reg.registerPrim(prim_NumRem));
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
    bindPrimitive(rt, N, "bitAnd:",   reg.registerPrim(prim_IntBitAnd));
    bindPrimitive(rt, N, "bitOr:",    reg.registerPrim(prim_IntBitOr));
    bindPrimitive(rt, N, "bitXor:",   reg.registerPrim(prim_IntBitXor));
    bindPrimitive(rt, N, "bitInvert", reg.registerPrim(prim_IntBitInvert));
    bindPrimitive(rt, N, "bitShift:", reg.registerPrim(prim_IntBitShift));
}

} // namespace protoST
