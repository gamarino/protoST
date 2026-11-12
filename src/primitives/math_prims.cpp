#include "protoST/STRuntime.h"
#include "runtime/NativeError.h"
#include "runtime/ClassedErrorSignal.h"
#include "primitives/IntegerDivision.h"
#include "runtime/TransientPin.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cstdint>

#include <cmath>
#include <stdexcept>
#include <string>

namespace protoST {

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

// Defined in int_prims.cpp: Pharo's adaptToNumber:andSend: double dispatch for
// an argument that is not a native number (a Fraction). nullptr if unsupported.
const proto::ProtoObject* adaptNumberArgument(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* arg,
                                              const char* selector);

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

// A number as a double, across the whole tower: a LargeInteger does not fit
// asLong, so it goes through its exact decimal digits and strtod (correctly
// rounded; beyond the Float range it becomes +/-inf, as in Pharo).
double asDoubleVal(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (v->isFloat(ctx)) return v->asDouble(ctx);
    if (v->isInteger(ctx) && v->compare(ctx, ctx->fromLong(LLONG_MAX)) <= 0
        && v->compare(ctx, ctx->fromLong(LLONG_MIN)) >= 0)
        return static_cast<double>(v->asLong(ctx));
    return std::strtod(formatNumber(ctx, v).c_str(), nullptr);
}

// The integer nearest below/at `d` as a SmallInteger or, beyond 64 bits, a
// LargeInteger (fromLong would overflow).
const proto::ProtoObject* integerFromDouble(proto::ProtoContext* ctx, double d) {
    if (std::isnan(d) || std::isinf(d))
        throw NativeError("cannot convert a non-finite Float to an Integer");
    if (d > -9.2e18 && d < 9.2e18) return ctx->fromLong(static_cast<long long>(d));
    char digits[400];
    std::snprintf(digits, sizeof(digits), "%.0f", d);
    return ctx->fromString(digits, 10);
}

void requireNumberArg(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                      const char* who) {
    if (!isNumber(ctx, v)) {
        throw NativeError(std::string(who) +
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
const proto::ProtoObject* prim_LogBase(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a,
                                       int argc) {
    if (argc != 1) throw NativeError("log: expects 1 arg (base)");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptNumberArgument(rt, ctx, r, a[0], "log:"))
            return adapted;
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
    return integerFromDouble(ctx, d);                                         \
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
    if (d == 0.0) ctx->raiseError(proto::ProtoErrorKind::DivisionByZero, "ZeroDivide");
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
// (the receiver wins a tie). `partialCompare` gives exact mixed-mode ordering;
// when either side is a NaN no ordering holds and, as `Magnitude>>min:`
// (`self < x ifTrue: [self] ifFalse: [x]`) defines, the argument is answered.
const proto::ProtoObject* prim_Min(STRuntime&, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int) {
    requireNumberArg(ctx, a[0], "min:");
    return (r->partialCompare(ctx, a[0]) <= 0) ? r : a[0];
}
const proto::ProtoObject* prim_Max(STRuntime&, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int) {
    requireNumberArg(ctx, a[0], "max:");
    return (r->partialCompare(ctx, a[0]) >= 0) ? r : a[0];
}

// `between:and:` — inclusive range test, `low <= self <= high`; false for NaN.
const proto::ProtoObject* prim_BetweenAnd(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a,
                                          int argc) {
    if (argc != 2) throw NativeError("between:and: expects 2 args");
    requireNumberArg(ctx, a[0], "between:and:");
    requireNumberArg(ctx, a[1], "between:and:");
    bool inside = r->partialCompare(ctx, a[0]) >= 0 &&
                  r->partialCompare(ctx, a[1]) <= 0;
    return inside ? PROTO_TRUE : PROTO_FALSE;
}

// `asFloat` — the receiver as a Float (an integer is widened).
const proto::ProtoObject* prim_AsFloat(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const*, int) {
    if (r->isFloat(ctx)) return r;
    return ctx->fromDouble(asDoubleVal(ctx, r));
}

// `asInteger` — the receiver as an integer (a Float is truncated toward zero).
const proto::ProtoObject* prim_AsInteger(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    if (!r->isFloat(ctx)) return r;
    return integerFromDouble(ctx, std::trunc(r->asDouble(ctx)));
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
        throw NativeError("factorial: receiver is not an integer");
    }
    if (r->integerSign(ctx) < 0) {
        throw NativeError("factorial: receiver is negative");
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
const proto::ProtoObject* prim_RaisedTo(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const* a,
                                        int argc) {
    if (argc != 1) throw NativeError("raisedTo: expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptNumberArgument(rt, ctx, r, a[0], "raisedTo:"))
            return adapted;
    requireNumberArg(ctx, a[0], "raisedTo:");
    const proto::ProtoObject* exp = a[0];
    // Exact integer^integer path: repeated multiply, LargeInteger-safe.
    if (!r->isFloat(ctx) && !exp->isFloat(ctx)) {
        long long e = exp->asLong(ctx);
        // Exponentiation by squaring: O(log e) multiplications, so large
        // powers of LargeIntegers do not build every intermediate power.
        const proto::ProtoObject* acc = ctx->fromLong(1);
        const proto::ProtoObject* base = r;
        TransientPin pinAcc(ctx, acc), pinBase(ctx, base);
        for (unsigned long long k = static_cast<unsigned long long>(e < 0 ? -e : e); k; k >>= 1) {
            if (k & 1) { acc = acc->multiply(ctx, base); pinAcc.reset(acc); }
            if (k > 1) { base = base->multiply(ctx, base); pinBase.reset(base); }
        }
        if (e >= 0) return acc;
        // Negative integer exponent: the exact reciprocal (1 / n^k is a
        // Fraction), as in Pharo.
        bool understood = false;
        const proto::ProtoObject* args[1] = { acc };
        const proto::ProtoObject* q = sendDynamic(rt, ctx, ctx->fromLong(1),
            proto::ProtoString::createSymbol(ctx, "/"), args, 1, &understood);
        if (understood && q) return q;
        return ctx->fromDouble(std::pow(asDoubleVal(ctx, r), static_cast<double>(e)));
    }
    // Any Float operand — libm pow, answer a Float.
    return ctx->fromDouble(std::pow(asDoubleVal(ctx, r),
                                    asDoubleVal(ctx, exp)));
}

// `raisedTo:modulo:` — modular exponentiation, Python's three-argument pow
// (R3): `b raisedTo: e modulo: m` is b^e mod m without building b^e.
//   * All three must be Integers (SmallInteger or LargeInteger); otherwise an
//     Error, as Python's TypeError.
//   * m = 0 signals ZeroDivide (Python: ValueError).
//   * The result takes the sign of m, like `\\`: in [0, m) for m > 0 and in
//     (m, 0] for m < 0. When |m| = 1 it is 0, whatever b and e.
//   * A negative e uses the inverse of b modulo |m|: b^-e = (b^-1)^e. When b
//     has none (gcd(b, m) ~= 1), ArithmeticError, as Python's ValueError
//     "base is not invertible for the given modulus".
// A modulus below 2^63 runs on machine words (a 128-bit product, or a
// shift-and-add product without one); a larger modulus on protoCore's exact
// integers. The exponent is read bit by bit, so its size costs only its
// length (Decimal's hash raises to exponents near 10^9 and beyond).
namespace {

// a * b mod n for a, b < n < 2^63.
std::uint64_t mulMod(std::uint64_t a, std::uint64_t b, std::uint64_t n) {
#if defined(__SIZEOF_INT128__)
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(a) * b) % n);
#else
    // n < 2^63, so a + a never wraps.
    std::uint64_t r = 0;
    while (b) {
        if (b & 1) { r += a; if (r >= n) r -= n; }
        a += a; if (a >= n) a -= n;
        b >>= 1;
    }
    return r;
#endif
}

// x mod n in [0, n), for n > 0 (protoCore's remainder takes x's sign).
const proto::ProtoObject* modPositive(proto::ProtoContext* ctx, const proto::ProtoObject* x,
                                      const proto::ProtoObject* n) {
    const proto::ProtoObject* r = integerRemainder(ctx, x, n);
    if (r->integerSign(ctx) < 0) r = r->add(ctx, n);
    return r;
}

// a^-1 mod n for 0 <= a < n and n > 1, or nullptr when gcd(a, n) ~= 1.
// Extended Euclid, keeping t with t * a = r (mod n).
const proto::ProtoObject* modInverse(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                                     const proto::ProtoObject* n) {
    const proto::ProtoObject* r0 = n;
    const proto::ProtoObject* r1 = a;
    const proto::ProtoObject* t0 = ctx->fromLong(0);
    const proto::ProtoObject* t1 = ctx->fromLong(1);
    TransientPin pinR0(ctx, r0), pinR1(ctx, r1), pinT0(ctx, t0), pinT1(ctx, t1);
    while (r1->integerSign(ctx) != 0) {
        const proto::ProtoObject* q = integerQuotient(ctx, r0, r1);
        TransientPin pinQ(ctx, q);
        const proto::ProtoObject* qr = q->multiply(ctx, r1);
        TransientPin pinQr(ctx, qr);
        const proto::ProtoObject* r2 = r0->subtract(ctx, qr);
        TransientPin pinR2(ctx, r2);
        const proto::ProtoObject* qt = q->multiply(ctx, t1);
        TransientPin pinQt(ctx, qt);
        const proto::ProtoObject* t2 = t0->subtract(ctx, qt);
        r0 = r1; pinR0.reset(r0);
        r1 = r2; pinR1.reset(r1);
        t0 = t1; pinT0.reset(t0);
        t1 = t2; pinT1.reset(t1);
    }
    if (r0->compare(ctx, ctx->fromLong(1)) != 0) return nullptr;
    return modPositive(ctx, t0, n);
}

// The value of an Integer when it fits a machine word (a LargeInteger below
// 2^63 in magnitude is read without allocating; asLong throws beyond).
bool integerWord(proto::ProtoContext* ctx, const proto::ProtoObject* v, long long& out) {
    if (proto::isSmallInt(v)) { out = v->asLong(ctx); return true; }
    try {
        out = v->asLong(ctx);
        return true;
    } catch (...) {
        return false;
    }
}

// a^-1 mod n for 0 <= a < n < 2^63, n > 1, on machine words; false when
// gcd(a, n) ~= 1. The Bezout coefficients stay below n in magnitude, and so
// does q * t1, so no step overflows.
bool modInverseWord(std::uint64_t a, std::uint64_t n, std::uint64_t& out) {
    long long r0 = static_cast<long long>(n), r1 = static_cast<long long>(a);
    long long t0 = 0, t1 = 1;
    while (r1 != 0) {
        const long long q = r0 / r1;
        const long long r2 = r0 - q * r1;
        r0 = r1; r1 = r2;
        const long long t2 = t0 - q * t1;
        t0 = t1; t1 = t2;
    }
    if (r0 != 1) return false;
    out = static_cast<std::uint64_t>(t0 < 0 ? t0 + static_cast<long long>(n) : t0);
    return true;
}

} // namespace

const proto::ProtoObject* prim_RaisedToModulo(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const* a,
                                              int argc) {
    if (argc != 2) throw NativeError("raisedTo:modulo: expects 2 args");
    auto isInt = [ctx](const proto::ProtoObject* v) {
        return v && v != PROTO_NONE && v->isInteger(ctx);
    };
    if (!isInt(r) || !isInt(a[0]) || !isInt(a[1]))
        throw NativeError("raisedTo:modulo: the receiver, the exponent and the modulus must be Integers");

    // All three on machine words: no allocation but the result's.
    long long bv = 0, ev = 0, mv = 0;
    if (integerWord(ctx, r, bv) && integerWord(ctx, a[0], ev) && integerWord(ctx, a[1], mv)
        && mv != LLONG_MIN) {
        if (mv == 0) ctx->raiseError(proto::ProtoErrorKind::DivisionByZero, "ZeroDivide");
        const auto nw = static_cast<std::uint64_t>(mv < 0 ? -mv : mv);
        if (nw == 1) return ctx->fromLong(0);
        long long bm = bv % static_cast<long long>(nw);
        if (bm < 0) bm += static_cast<long long>(nw);
        std::uint64_t bw = static_cast<std::uint64_t>(bm);
        std::uint64_t e = ev < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(ev)
                                 : static_cast<std::uint64_t>(ev);
        if (ev < 0 && !modInverseWord(bw, nw, bw))
            throw ClassedErrorSignal("ArithmeticError",
                "raisedTo:modulo: the base is not invertible for the given modulus");
        std::uint64_t acc = 1 % nw;
        while (e) {
            if (e & 1) acc = mulMod(acc, bw, nw);
            bw = mulMod(bw, bw, nw);
            e >>= 1;
        }
        long long res = static_cast<long long>(acc);
        // A negative modulus: the result in (m, 0], as Python's pow and `\\`.
        if (mv < 0 && res != 0) res -= static_cast<long long>(nw);
        return ctx->fromLong(res);
    }

    const proto::ProtoObject* modulus = a[1];
    const int modSign = modulus->integerSign(ctx);
    if (modSign == 0)
        ctx->raiseError(proto::ProtoErrorKind::DivisionByZero, "ZeroDivide");
    const proto::ProtoObject* n = modulus->abs(ctx);   // |m|
    TransientPin pinN(ctx, n);
    if (n->compare(ctx, ctx->fromLong(1)) == 0) return ctx->fromLong(0);

    const proto::ProtoObject* base = modPositive(ctx, r, n);
    TransientPin pinBase(ctx, base);
    const proto::ProtoObject* exponent = a[0];
    if (exponent->integerSign(ctx) < 0) {
        base = modInverse(ctx, base, n);
        if (!base)
            throw ClassedErrorSignal("ArithmeticError",
                "raisedTo:modulo: the base is not invertible for the given modulus");
        pinBase.reset(base);
        exponent = exponent->abs(ctx);
    }
    // The exponent's bits, most significant first.
    const std::string bits = exponent->asIntegerString(ctx, 2)->toStdString(ctx);

    const proto::ProtoObject* result = nullptr;
    long long nl = 0;
    if (integerWord(ctx, n, nl)) {
        const auto nw = static_cast<std::uint64_t>(nl);
        const auto bw = static_cast<std::uint64_t>(base->asLong(ctx));
        std::uint64_t acc = 1 % nw;
        for (char bit : bits) {
            acc = mulMod(acc, acc, nw);
            if (bit == '1') acc = mulMod(acc, bw, nw);
        }
        result = ctx->fromLong(static_cast<long long>(acc));
    } else {
        const proto::ProtoObject* acc = ctx->fromLong(1);
        TransientPin pinAcc(ctx, acc);
        for (char bit : bits) {
            const proto::ProtoObject* sq = acc->multiply(ctx, acc);
            pinAcc.reset(sq);
            acc = modPositive(ctx, sq, n);
            pinAcc.reset(acc);
            if (bit == '1') {
                const proto::ProtoObject* prod = acc->multiply(ctx, base);
                pinAcc.reset(prod);
                acc = modPositive(ctx, prod, n);
                pinAcc.reset(acc);
            }
        }
        result = acc;
    }
    // A negative modulus: the result in (m, 0], as Python's pow and `\\`.
    if (modSign < 0 && result->integerSign(ctx) != 0) {
        TransientPin pinResult(ctx, result);
        result = result->subtract(ctx, n);
    }
    return result;
}

// `gcd:` — greatest common divisor of two integers (Euclid). Bignum-safe: the
// remainder steps go through protoCore `modulo`, so a LargeInteger argument
// works. `0 gcd: 0` is undefined and raises.
const proto::ProtoObject* prim_Gcd(STRuntime& rt, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw NativeError("gcd: expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptNumberArgument(rt, ctx, r, a[0], "gcd:"))
            return adapted;
    requireNumberArg(ctx, a[0], "gcd:");
    if (r->isFloat(ctx) || a[0]->isFloat(ctx)) {
        throw NativeError("gcd: operands must be integers");
    }
    const proto::ProtoObject* x = r->abs(ctx);
    const proto::ProtoObject* y = a[0]->abs(ctx);
    if (x->integerSign(ctx) == 0 && y->integerSign(ctx) == 0) {
        throw NativeError("gcd: of zero and zero is undefined");
    }
    while (y->integerSign(ctx) != 0) {
        const proto::ProtoObject* t = integerRemainder(ctx, x, y);
        x = y;
        y = t;
    }
    return x;
}

// `lcm:` — least common multiple, `|a * b| / gcd(a, b)`. Bignum-safe.
const proto::ProtoObject* prim_Lcm(STRuntime& rt, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* r,
                                   const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw NativeError("lcm: expects 1 arg");
    if (!isNumber(ctx, a[0]))
        if (const proto::ProtoObject* adapted = adaptNumberArgument(rt, ctx, r, a[0], "lcm:"))
            return adapted;
    requireNumberArg(ctx, a[0], "lcm:");
    if (r->isFloat(ctx) || a[0]->isFloat(ctx)) {
        throw NativeError("lcm: operands must be integers");
    }
    if (r->integerSign(ctx) == 0 || a[0]->integerSign(ctx) == 0) {
        return ctx->fromLong(0);
    }
    const proto::ProtoObject* g = prim_Gcd(rt, ctx, r, a, 1);
    const proto::ProtoObject* prod = r->multiply(ctx, a[0])->abs(ctx);
    return integerQuotient(ctx, prod, g);
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

// aFloat __floatParts → #(mantissa exponent), two Integers with
// mantissa * 2^exponent exactly equal to the receiver (mantissa odd, or 0).
// The kernel's asExactFraction builds the Integer or Fraction from them.
const proto::ProtoObject* prim_FloatParts(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const*, int) {
    const double d = r->asDouble(ctx);
    if (!std::isfinite(d)) throw NativeError("__floatParts: not a finite Float");
    int e = 0;
    const double m = std::frexp(d, &e);
    long long mant = static_cast<long long>(std::ldexp(m, 53));
    long long exp2 = static_cast<long long>(e) - 53;
    if (mant == 0) exp2 = 0;
    while (mant != 0 && (mant & 1) == 0) { mant /= 2; ++exp2; }
    const proto::ProtoList* data = ctx->newList();
    TransientPin pinData(ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    data = data->appendLast(ctx, ctx->fromLong(mant));
    pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
    data = data->appendLast(ctx, ctx->fromLong(exp2));
    pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
    const proto::ProtoObject* arr = rt.bootstrap().arrayProto->newChild(ctx, /*isMutable=*/true);
    TransientPin pinArr(ctx, arr);
    arr->setAttribute(ctx, rt.bootstrap().sym.data, data->asObject(ctx));
    return arr;
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
    bindPrimitive(rt, N, "raisedTo:modulo:", reg.registerPrim(prim_RaisedToModulo));
    bindPrimitive(rt, N, "__floatParts", reg.registerPrim(prim_FloatParts));
    bindPrimitive(rt, N, "gcd:",        reg.registerPrim(prim_Gcd));
    bindPrimitive(rt, N, "lcm:",        reg.registerPrim(prim_Lcm));

    // Class-side constants of `Float`, on its metaclass (D52).
    auto* F = b.floatProto;
    bindClassPrimitive(rt, F, "pi",       reg.registerPrim(prim_Pi));
    bindClassPrimitive(rt, F, "e",        reg.registerPrim(prim_E));
    bindClassPrimitive(rt, F, "infinity", reg.registerPrim(prim_Infinity));
    bindClassPrimitive(rt, F, "nan",      reg.registerPrim(prim_Nan));
}

} // namespace protoST
