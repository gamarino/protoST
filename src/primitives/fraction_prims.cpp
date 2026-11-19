#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <cmath>
#include <cstdint>

namespace protoST {

// Fraction comparisons (lib/kernel/fraction.st).
//
// A Fraction is a kernel class written in Smalltalk: two instance variables,
// numerator and denominator, each an Integer of any size. Its comparisons
// used to subtract (a - b builds b negated and then the sum, each a new
// Fraction normalized by a gcd) and read the sign of the result's numerator,
// some forty message sends per comparison; against a Float they rounded the
// Fraction to a Float. The primitives below compare in one call and exactly:
//
//   n1/d1 against n2/d2   sign(n1*d2 - n2*d1) * sign(d1) * sign(d2)
//   n/d against x         x = m * 2^e exactly (frexp), so the order is that
//                         of n * 2^max(0,-e) against m * d * 2^max(0,e)
//
// When every operand is a SmallInteger (54 bits with sign) the products fit
// a 128-bit integer and nothing is allocated; otherwise protoCore's exact
// Integer arithmetic does the multiplications. No gcd is computed, so an
// unnormalized Fraction (2/4, built with basicNew) compares by its value.
//
// aFraction __fractionCompare: other by: selector answers `aFraction
// selector other`; aFraction __fractionCompareFrom: other by: selector
// answers `other selector aFraction` (the adaptToNumber:andSend: direction).
// selector is one of < <= > >= = ~=. Each answers nil, for the kernel's
// general code to decide, when it does not order the pair exactly: a NaN
// (whose answers the kernel keeps as they were), a non-number or a Decimal,
// an object of another class, a zero denominator, another selector.

namespace {

using PO = proto::ProtoObject;

// The selector's test of a three-way order c (-1, 0 or 1).
enum class CmpOp { Lt, Le, Gt, Ge, Eq, Ne, None };

CmpOp opOf(const Bootstrap& b, const PO* selector) {
    const auto* s = reinterpret_cast<const proto::ProtoString*>(selector);
    if (s == b.sym.lessThan) return CmpOp::Lt;
    if (s == b.sym.lessOrEqual) return CmpOp::Le;
    if (s == b.sym.greaterThan) return CmpOp::Gt;
    if (s == b.sym.greaterOrEqual) return CmpOp::Ge;
    if (s == b.sym.equal) return CmpOp::Eq;
    if (s == b.sym.notEqual) return CmpOp::Ne;
    return CmpOp::None;
}

bool holds(CmpOp op, int c) {
    switch (op) {
        case CmpOp::Lt: return c < 0;
        case CmpOp::Le: return c <= 0;
        case CmpOp::Gt: return c > 0;
        case CmpOp::Ge: return c >= 0;
        case CmpOp::Eq: return c == 0;
        case CmpOp::Ne: return c != 0;
        case CmpOp::None: break;
    }
    return false;
}

int signOf(long long v) { return (v > 0) - (v < 0); }

bool isIntegerValue(proto::ProtoContext* ctx, const PO* v) {
    return v && v != PROTO_NONE && (proto::isSmallInt(v) || v->isInteger(ctx));
}

int integerSign(proto::ProtoContext* ctx, const PO* v) {
    return proto::isSmallInt(v) ? signOf(proto::asSmallInt(v)) : v->integerSign(ctx);
}

int clampSign(int c) { return (c > 0) - (c < 0); }

// The order of a against b, two Integers of any size.
int compareIntegers(proto::ProtoContext* ctx, const PO* a, const PO* b) {
    return clampSign(a->compare(ctx, b));
}

// A rational: an Integer numerator over a non-zero Integer denominator.
struct Rational {
    const PO* num = nullptr;
    const PO* den = nullptr;
};

// The receiver's numerator and denominator, if it is a Fraction (both
// instance variables Integers and the denominator not zero).
bool fractionParts(const Bootstrap& b, proto::ProtoContext* ctx, const PO* v, Rational& out) {
    if (!v || v == PROTO_NONE || proto::isSmallInt(v)) return false;
    const PO* n = v->getAttribute(ctx, b.sym.ivNumerator);
    const PO* d = v->getAttribute(ctx, b.sym.ivDenominator);
    if (!isIntegerValue(ctx, n) || !isIntegerValue(ctx, d)) return false;
    if (integerSign(ctx, d) == 0) return false;
    out.num = n;
    out.den = d;
    return true;
}

// sign(n1/d1 - n2/d2) for Integer operands, d1 and d2 not zero.
int compareRationals(proto::ProtoContext* ctx, const Rational& x, const Rational& y) {
    const int denSigns = integerSign(ctx, x.den) * integerSign(ctx, y.den);
    if (proto::isSmallInt(x.num) && proto::isSmallInt(x.den)
        && proto::isSmallInt(y.num) && proto::isSmallInt(y.den)) {
        const long long n1 = proto::asSmallInt(x.num), d1 = proto::asSmallInt(x.den);
        const long long n2 = proto::asSmallInt(y.num), d2 = proto::asSmallInt(y.den);
#if defined(__SIZEOF_INT128__)
        // |n| and |d| are at most 2^53, so each product is at most 2^106.
        const __int128 lhs = static_cast<__int128>(n1) * d2;
        const __int128 rhs = static_cast<__int128>(n2) * d1;
        return ((lhs > rhs) - (lhs < rhs)) * denSigns;
#else
        // Without a 128-bit type: the products are exact in 64 bits when
        // they fit, which a floating-point estimate decides safely.
        const double p1 = static_cast<double>(n1) * static_cast<double>(d2);
        const double p2 = static_cast<double>(n2) * static_cast<double>(d1);
        if (std::fabs(p1) < 9.0e18 && std::fabs(p2) < 9.0e18) {
            const long long lhs = n1 * d2, rhs = n2 * d1;
            return ((lhs > rhs) - (lhs < rhs)) * denSigns;
        }
#endif
    }
    // The operands are reachable from the receiver and the argument, which
    // the calling frame roots; they are pinned anyway, so that no Integer
    // this function reads is held only by a C++ local across an allocation.
    TransientPin pinN1(ctx, x.num);
    TransientPin pinD1(ctx, x.den);
    TransientPin pinN2(ctx, y.num);
    TransientPin pinD2(ctx, y.den);
    const PO* lhs = x.num->multiply(ctx, y.den);
    TransientPin pinLhs(ctx, lhs);
    const PO* rhs = y.num->multiply(ctx, x.den);
    TransientPin pinRhs(ctx, rhs);
    return compareIntegers(ctx, lhs, rhs) * denSigns;
}

#if defined(__SIZEOF_INT128__)
int bitLength(unsigned __int128 v) {
    int n = 0;
    while (v) { v >>= 1; ++n; }
    return n;
}
#endif

// sign(n/d - x) for a finite, non-zero x, d not zero.
int compareRationalWithDouble(proto::ProtoContext* ctx, const Rational& q, double x) {
    int e = 0;
    const double frac = std::frexp(x, &e);                   // x = frac * 2^e, 0.5 <= |frac| < 1
    const long long mant = static_cast<long long>(std::ldexp(frac, 53));   // exact, |mant| < 2^53
    const int exp2 = e - 53;                                  // x = mant * 2^exp2
    const int denSign = integerSign(ctx, q.den);
#if defined(__SIZEOF_INT128__)
    if (proto::isSmallInt(q.num) && proto::isSmallInt(q.den)) {
        // Compare n * 2^max(0,-exp2) with mant * d * 2^max(0,exp2). Each side
        // is computed in 128 bits only when it stays below 2^126; a larger
        // side exceeds the other, which is below 2^106, so its sign decides.
        const long long n = proto::asSmallInt(q.num);
        const __int128 md = static_cast<__int128>(mant) * proto::asSmallInt(q.den);
        auto magnitude = [](__int128 v) {
            return static_cast<unsigned __int128>(v < 0 ? -v : v);
        };
        auto sign128 = [](__int128 v) { return (v > 0) - (v < 0); };
        int c;
        if (exp2 >= 0) {
            if (bitLength(magnitude(md)) + exp2 > 126) c = -sign128(md);
            else {
                const __int128 rhs = md * (static_cast<__int128>(1) << exp2);
                c = (n > rhs) - (n < rhs);
            }
        } else {
            const int shift = -exp2;
            if (n == 0) c = -sign128(md);
            else if (bitLength(magnitude(n)) + shift > 126) c = signOf(n);
            else {
                const __int128 lhs = static_cast<__int128>(n) * (static_cast<__int128>(1) << shift);
                c = (lhs > md) - (lhs < md);
            }
        }
        return c * denSign;
    }
#endif
    TransientPin pinDen(ctx, q.den);   // as in compareRationals
    const PO* mantissa = ctx->fromLong(mant);
    TransientPin pinMantissa(ctx, mantissa);
    const PO* rhs = mantissa->multiply(ctx, q.den);
    TransientPin pinRhs(ctx, rhs);
    const PO* lhs = q.num;
    TransientPin pinLhs(ctx, lhs);
    if (exp2 >= 0) {
        rhs = rhs->shiftLeft(ctx, exp2);
        pinRhs.reset(rhs);
    } else {
        lhs = lhs->shiftLeft(ctx, -exp2);
        pinLhs.reset(lhs);
    }
    return compareIntegers(ctx, lhs, rhs) * denSign;
}

// Whether the pair is ordered here, and if so sign(fraction - other) in *c.
bool compareFractionWith(const Bootstrap& b, proto::ProtoContext* ctx, const PO* fraction,
                         const PO* other, int* c) {
    Rational self;
    if (!fractionParts(b, ctx, fraction, self)) return false;
    if (!other || other == PROTO_NONE) return false;
    if (isIntegerValue(ctx, other)) {
        const Rational y{other, ctx->fromInteger(1)};
        *c = compareRationals(ctx, self, y);
        return true;
    }
    if (other->isFloat(ctx)) {
        const double x = other->asDouble(ctx);
        if (std::isnan(x)) return false;
        if (std::isinf(x)) { *c = x > 0 ? -1 : 1; return true; }
        if (x == 0.0) {
            *c = integerSign(ctx, self.num) * integerSign(ctx, self.den);
            return true;
        }
        *c = compareRationalWithDouble(ctx, self, x);
        return true;
    }
    // Another Fraction: an instance of the receiver's own class. A subclass
    // or another kind of number is left to the kernel's general code.
    if (other->getFirstParent(ctx) != fraction->getFirstParent(ctx)) return false;
    Rational y;
    if (!fractionParts(b, ctx, other, y)) return false;
    *c = compareRationals(ctx, self, y);
    return true;
}

const PO* prim_FractionCompare(STRuntime& rt, proto::ProtoContext* ctx, const PO* r,
                               const PO* const* a, int argc) {
    if (argc != 2) return PROTO_NONE;
    const CmpOp op = opOf(rt.bootstrap(), a[1]);
    int c = 0;
    if (op == CmpOp::None || !compareFractionWith(rt.bootstrap(), ctx, r, a[0], &c))
        return PROTO_NONE;
    return holds(op, c) ? PROTO_TRUE : PROTO_FALSE;
}

const PO* prim_FractionCompareFrom(STRuntime& rt, proto::ProtoContext* ctx, const PO* r,
                                   const PO* const* a, int argc) {
    if (argc != 2) return PROTO_NONE;
    const CmpOp op = opOf(rt.bootstrap(), a[1]);
    int c = 0;
    if (op == CmpOp::None || !compareFractionWith(rt.bootstrap(), ctx, r, a[0], &c))
        return PROTO_NONE;
    return holds(op, -c) ? PROTO_TRUE : PROTO_FALSE;
}

} // namespace

void installFractionPrimitives(STRuntime& rt) {
    // On the shared number prototype, which Fraction (a Number subclass)
    // inherits; a native receiver is not a Fraction and answers nil.
    const PO* N = rt.bootstrap().numberProto;
    bindPrimitive(rt, N, "__fractionCompare:by:", primitiveMethod<prim_FractionCompare>);
    bindPrimitive(rt, N, "__fractionCompareFrom:by:", primitiveMethod<prim_FractionCompareFrom>);
}

} // namespace protoST
