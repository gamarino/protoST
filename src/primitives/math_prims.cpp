#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace protoST {

// T4-b — the mathematical protocol (Track 4, sub-slice b).
//
// The stdlib spec sketched `Math` as a loadable `.st` module, but the
// idiomatic Smalltalk form is a unary / keyword message sent to a number —
// `9 sqrt`, `x sin`, `30 factorial`, `2 raisedTo: 100` — which must work with
// no `Import`. So the math operations are C++ primitives bootstrapped onto the
// shared `numberProto` (D11 / D20), and a small set of constants is bound
// class-side on `Float` (`Float pi`, `Float e`, ...). They are always
// available, exactly like `+` and `printString`.
//
// Three groups:
//   * Transcendental / irrational — thin wrappers over `<cmath>` (libm). Each
//     coerces the receiver to a `double` and answers a `Float`.
//   * Rounding — `floor` / `ceiling` / `rounded` / `truncated`. An integer
//     receiver answers itself; a `Float` is rounded and answered as an integer.
//   * Pure / algebraic — `sign`, `squared`, `reciprocal`, `isZero`, `min:`,
//     `max:`, `between:and:`, `asFloat`, `asInteger`, `even`/`odd`,
//     `factorial`, `raisedTo:`, `gcd:`, `lcm:`. These delegate to the existing
//     protoCore arithmetic / comparison so they inherit mixed-mode coercion
//     and — crucially for `factorial` and integer `raisedTo:` — transparent
//     `LargeInteger` promotion: `30 factorial` and `2 raisedTo: 100` stay
//     exact, never overflow.
//
// Domain errors. A libm domain error (`(-1) sqrt`, `0 ln`) is NOT turned into
// a protoST `Error`; the IEEE-754 result (`nan` / `-inf`) is let through, the
// same contract libm itself offers. This keeps the math protocol total — no
// math message ever raises — and is documented in LANGUAGE.md §12.2. Argument
// validation (a non-number argument to `min:` etc.) and the genuinely
// undefined integer cases (`factorial` of a negative, `gcd:` of zero/zero) DO
// raise a catchable `Error`.

namespace {

double asDoubleVal(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v->isFloat(ctx) ? v->asDouble(ctx)
                           : static_cast<double>(v->asLong(ctx));
}

void requireNumberArg(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                      const char* who) {
    if (!isNumber(ctx, v)) {
        throw std::runtime_error(std::string(who) +
                                 ": argument is not a number");
    }
}

// ----------------------- transcendental / irrational -----------------------
// One macro covers every unary libm function: coerce the receiver to a double,
// apply `FN`, answer a Float.
#define DEF_UNARY_MATH(NAME, FN)                                              \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,   \
                                      const proto::ProtoObject* r,            \
                                      const proto::ProtoObject* const*,       \
                                      int) {                                  \
    return ctx->fromDouble(FN(asDoubleVal(ctx, r)));                          \
}

DEF_UNARY_MATH(Sqrt,   std::sqrt)
DEF_UNARY_MATH(Sin,    std::sin)
DEF_UNARY_MATH(Cos,    std::cos)
DEF_UNARY_MATH(Tan,    std::tan)
DEF_UNARY_MATH(ArcSin, std::asin)
DEF_UNARY_MATH(ArcCos, std::acos)
DEF_UNARY_MATH(ArcTan, std::atan)
DEF_UNARY_MATH(Ln,     std::log)
DEF_UNARY_MATH(Exp,    std::exp)
DEF_UNARY_MATH(Log10,  std::log10)

// `log:` — logarithm in an arbitrary base. `n log: b` == ln(n) / ln(b).
const proto::ProtoObject* prim_LogBase(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a,
                                       int argc) {
    if (argc != 1) throw std::runtime_error("log: expects 1 arg (base)");
    requireNumberArg(ctx, a[0], "log:");
    return ctx->fromDouble(std::log(asDoubleVal(ctx, r)) /
                           std::log(asDoubleVal(ctx, a[0])));
}

// ------------------------------- rounding ----------------------------------
// An integer receiver already is its own floor/ceiling/rounded/truncated.
// A Float is rounded by libm and answered as a SmallInteger / LargeInteger.
#define DEF_ROUNDING(NAME, FN)                                                \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,   \
                                      const proto::ProtoObject* r,            \
                                      const proto::ProtoObject* const*,       \
                                      int) {                                  \
    if (!r->isFloat(ctx)) return r;                                           \
    double d = FN(r->asDouble(ctx));                                          \
    return ctx->fromLong(static_cast<long long>(d));                          \
}

DEF_ROUNDING(Floor,     std::floor)
DEF_ROUNDING(Ceiling,   std::ceil)
DEF_ROUNDING(Rounded,   std::round)
DEF_ROUNDING(Truncated, std::trunc)

// ---------------------------- pure / algebraic -----------------------------

// `sign` — -1 / 0 / +1. Bignum-safe for an integer; sign of the double else.
const proto::ProtoObject* prim_Sign(STRuntime&, proto::ProtoContext* ctx,
                                    const proto::ProtoObject* r,
                                    const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) {
        double d = r->asDouble(ctx);
        return ctx->fromLong(d > 0.0 ? 1 : (d < 0.0 ? -1 : 0));
    }
    return ctx->fromLong(r->integerSign(ctx));
}

// `squared` — self * self. Delegates to protoCore `multiply`, so an integer
// square that overflows promotes to a LargeInteger automatically.
const proto::ProtoObject* prim_Squared(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const*, int) {
    return r->multiply(ctx, r);
}

// `reciprocal` — 1 / self, always as a Float (protoST has no Fraction type, so
// `4 reciprocal` is `0.25`, not a truncated `0`).
const proto::ProtoObject* prim_Reciprocal(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const*, int) {
    double d = asDoubleVal(ctx, r);
    if (d == 0.0) throw std::runtime_error("ZeroDivide");
    return ctx->fromDouble(1.0 / d);
}

// `isZero` — comparison with zero across the whole tower.
const proto::ProtoObject* prim_IsZero(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const*, int) {
    bool zero = r->isFloat(ctx) ? (r->asDouble(ctx) == 0.0)
                                : (r->integerSign(ctx) == 0);
    return zero ? PROTO_TRUE : PROTO_FALSE;
}

// `min:` / `max:` — answer the smaller / larger of the receiver and argument
// (the receiver wins a tie). `compare` gives mixed-mode ordering for free.
const proto::ProtoObject* prim_Min(STRuntime&, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int) {
    requireNumberArg(ctx, a[0], "min:");
    return (r->compare(ctx, a[0]) <= 0) ? r : a[0];
}
const proto::ProtoObject* prim_Max(STRuntime&, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int) {
    requireNumberArg(ctx, a[0], "max:");
    return (r->compare(ctx, a[0]) >= 0) ? r : a[0];
}

// `between:and:` — inclusive range test, `low <= self <= high`.
const proto::ProtoObject* prim_BetweenAnd(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a,
                                          int argc) {
    if (argc != 2) throw std::runtime_error("between:and: expects 2 args");
    requireNumberArg(ctx, a[0], "between:and:");
    requireNumberArg(ctx, a[1], "between:and:");
    bool inside = r->compare(ctx, a[0]) >= 0 && r->compare(ctx, a[1]) <= 0;
    return inside ? PROTO_TRUE : PROTO_FALSE;
}

// `asFloat` — the receiver as a Float (an integer is widened).
const proto::ProtoObject* prim_AsFloat(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) return r;
    return ctx->fromDouble(static_cast<double>(r->asLong(ctx)));
}

// `asInteger` — the receiver as an integer (a Float is truncated toward zero).
const proto::ProtoObject* prim_AsInteger(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    if (!r->isFloat(ctx)) return r;
    return ctx->fromLong(static_cast<long long>(std::trunc(r->asDouble(ctx))));
}

// `even` / `odd` — integer-parity aliases. A non-integral Float is neither.
const proto::ProtoObject* prim_Even(STRuntime&, proto::ProtoContext* ctx,
                                    const proto::ProtoObject* r,
                                    const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) return PROTO_FALSE;
    return (r->modulo(ctx, ctx->fromLong(2))->asLong(ctx) == 0)
               ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_Odd(STRuntime&, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) return PROTO_FALSE;
    return (r->modulo(ctx, ctx->fromLong(2))->asLong(ctx) != 0)
               ? PROTO_TRUE : PROTO_FALSE;
}

// `factorial` — iterative product `1 * 2 * ... * n` on a non-negative integer.
// Each step is a protoCore `multiply`, so the running product promotes to a
// LargeInteger the instant it leaves the 56-bit SmallInteger range; `30
// factorial` is therefore the exact 265252859812191058636308480000000.
const proto::ProtoObject* prim_Factorial(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) {
        throw std::runtime_error("factorial: receiver is not an integer");
    }
    if (r->integerSign(ctx) < 0) {
        throw std::runtime_error("factorial: receiver is negative");
    }
    long long n = r->asLong(ctx);
    const proto::ProtoObject* acc = ctx->fromLong(1);
    for (long long i = 2; i <= n; ++i) {
        acc = acc->multiply(ctx, ctx->fromLong(i));
    }
    return acc;
}

// `raisedTo:` — exponentiation.
//   * An integer exponent (>= 0) is computed by exact repeated multiplication,
//     so `2 raisedTo: 100` is the exact 2^100 (a LargeInteger), never an
//     overflowed double. A negative integer exponent answers a Float (the
//     reciprocal — protoST has no Fraction type).
//   * A Float exponent goes through libm `pow` and answers a Float.
const proto::ProtoObject* prim_RaisedTo(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const* a,
                                        int argc) {
    if (argc != 1) throw std::runtime_error("raisedTo: expects 1 arg");
    requireNumberArg(ctx, a[0], "raisedTo:");
    const proto::ProtoObject* exp = a[0];
    // Exact integer^integer path: repeated multiply, LargeInteger-safe.
    if (!r->isFloat(ctx) && !exp->isFloat(ctx)) {
        long long e = exp->asLong(ctx);
        if (e >= 0) {
            const proto::ProtoObject* acc = ctx->fromLong(1);
            for (long long i = 0; i < e; ++i) acc = acc->multiply(ctx, r);
            return acc;
        }
        // Negative integer exponent — answer a Float reciprocal.
        return ctx->fromDouble(std::pow(asDoubleVal(ctx, r),
                                        static_cast<double>(e)));
    }
    // Any Float operand — libm pow, answer a Float.
    return ctx->fromDouble(std::pow(asDoubleVal(ctx, r),
                                    asDoubleVal(ctx, exp)));
}

// `gcd:` — greatest common divisor of two integers (Euclid). Bignum-safe: the
// remainder steps go through protoCore `modulo`, so a LargeInteger argument
// works. `0 gcd: 0` is undefined and raises.
const proto::ProtoObject* prim_Gcd(STRuntime&, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("gcd: expects 1 arg");
    requireNumberArg(ctx, a[0], "gcd:");
    if (r->isFloat(ctx) || a[0]->isFloat(ctx)) {
        throw std::runtime_error("gcd: operands must be integers");
    }
    const proto::ProtoObject* x = r->abs(ctx);
    const proto::ProtoObject* y = a[0]->abs(ctx);
    if (x->integerSign(ctx) == 0 && y->integerSign(ctx) == 0) {
        throw std::runtime_error("gcd: of zero and zero is undefined");
    }
    while (y->integerSign(ctx) != 0) {
        const proto::ProtoObject* t = x->modulo(ctx, y);
        x = y;
        y = t;
    }
    return x;
}

// `lcm:` — least common multiple, `|a * b| / gcd(a, b)`. Bignum-safe.
const proto::ProtoObject* prim_Lcm(STRuntime& rt, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("lcm: expects 1 arg");
    requireNumberArg(ctx, a[0], "lcm:");
    if (r->isFloat(ctx) || a[0]->isFloat(ctx)) {
        throw std::runtime_error("lcm: operands must be integers");
    }
    if (r->integerSign(ctx) == 0 || a[0]->integerSign(ctx) == 0) {
        return ctx->fromLong(0);
    }
    const proto::ProtoObject* g = prim_Gcd(rt, ctx, r, a, 1);
    const proto::ProtoObject* prod = r->multiply(ctx, a[0])->abs(ctx);
    return prod->divide(ctx, g);
}

// ----------------------- class-side Float constants ------------------------
// Bound on `floatProto`; the receiver of `Float pi` is the prototype itself.
const proto::ProtoObject* prim_Pi(STRuntime&, proto::ProtoContext* ctx,
                                  const proto::ProtoObject*,
                                  const proto::ProtoObject* const*, int) {
    return ctx->fromDouble(M_PI);
}
const proto::ProtoObject* prim_E(STRuntime&, proto::ProtoContext* ctx,
                                 const proto::ProtoObject*,
                                 const proto::ProtoObject* const*, int) {
    return ctx->fromDouble(M_E);
}
const proto::ProtoObject* prim_Infinity(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject*,
                                        const proto::ProtoObject* const*, int) {
    return ctx->fromDouble(HUGE_VAL);
}
const proto::ProtoObject* prim_Nan(STRuntime&, proto::ProtoContext* ctx,
                                   const proto::ProtoObject*,
                                   const proto::ProtoObject* const*, int) {
    return ctx->fromDouble(std::nan(""));
}

} // anon

void installMathPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    auto* N   = b.numberProto;

    // Transcendental / irrational — each answers a Float.
    bindPrimitive(rt, N, "sqrt",   reg.registerPrim(prim_Sqrt));
    bindPrimitive(rt, N, "sin",    reg.registerPrim(prim_Sin));
    bindPrimitive(rt, N, "cos",    reg.registerPrim(prim_Cos));
    bindPrimitive(rt, N, "tan",    reg.registerPrim(prim_Tan));
    bindPrimitive(rt, N, "arcSin", reg.registerPrim(prim_ArcSin));
    bindPrimitive(rt, N, "arcCos", reg.registerPrim(prim_ArcCos));
    bindPrimitive(rt, N, "arcTan", reg.registerPrim(prim_ArcTan));
    bindPrimitive(rt, N, "ln",     reg.registerPrim(prim_Ln));
    bindPrimitive(rt, N, "exp",    reg.registerPrim(prim_Exp));
    bindPrimitive(rt, N, "log",    reg.registerPrim(prim_Log10));
    bindPrimitive(rt, N, "log:",   reg.registerPrim(prim_LogBase));

    // Rounding — answer an integer.
    bindPrimitive(rt, N, "floor",     reg.registerPrim(prim_Floor));
    bindPrimitive(rt, N, "ceiling",   reg.registerPrim(prim_Ceiling));
    bindPrimitive(rt, N, "rounded",   reg.registerPrim(prim_Rounded));
    bindPrimitive(rt, N, "truncated", reg.registerPrim(prim_Truncated));

    // Pure / algebraic.
    bindPrimitive(rt, N, "sign",        reg.registerPrim(prim_Sign));
    bindPrimitive(rt, N, "squared",     reg.registerPrim(prim_Squared));
    bindPrimitive(rt, N, "reciprocal",  reg.registerPrim(prim_Reciprocal));
    bindPrimitive(rt, N, "isZero",      reg.registerPrim(prim_IsZero));
    bindPrimitive(rt, N, "min:",        reg.registerPrim(prim_Min));
    bindPrimitive(rt, N, "max:",        reg.registerPrim(prim_Max));
    bindPrimitive(rt, N, "between:and:",reg.registerPrim(prim_BetweenAnd));
    bindPrimitive(rt, N, "asFloat",     reg.registerPrim(prim_AsFloat));
    bindPrimitive(rt, N, "asInteger",   reg.registerPrim(prim_AsInteger));
    bindPrimitive(rt, N, "even",        reg.registerPrim(prim_Even));
    bindPrimitive(rt, N, "odd",         reg.registerPrim(prim_Odd));
    bindPrimitive(rt, N, "factorial",   reg.registerPrim(prim_Factorial));
    bindPrimitive(rt, N, "raisedTo:",   reg.registerPrim(prim_RaisedTo));
    bindPrimitive(rt, N, "gcd:",        reg.registerPrim(prim_Gcd));
    bindPrimitive(rt, N, "lcm:",        reg.registerPrim(prim_Lcm));

    // Class-side constants on `Float` (the receiver is `floatProto` itself).
    auto* F = b.floatProto;
    bindPrimitive(rt, F, "pi",       reg.registerPrim(prim_Pi));
    bindPrimitive(rt, F, "e",        reg.registerPrim(prim_E));
    bindPrimitive(rt, F, "infinity", reg.registerPrim(prim_Infinity));
    bindPrimitive(rt, F, "nan",      reg.registerPrim(prim_Nan));
}

} // namespace protoST
