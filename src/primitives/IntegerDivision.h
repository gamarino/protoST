#pragma once
// Integer division that does not trust protoCore's divide/modulo for large
// divisors: with a divisor of 64 bits or more and a large quotient they answer
// wrong values ((2 raisedTo: 140) // (2 raisedTo: 70) gave 2^65 - 1). For
// divisors that fit in 62 bits protoCore's operations are used; otherwise a
// binary long division on the magnitudes, O(n^2) in the bit length.

#include "runtime/TransientPin.h"
#include "protoCore.h"

namespace protoST {

inline bool fitsDivisorWord(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    constexpr long long kLimit = 1LL << 62;
    return v->compare(ctx, ctx->fromLong(kLimit)) < 0
        && v->compare(ctx, ctx->fromLong(-kLimit)) > 0;
}

// Truncating division of integers: quotient rounded toward zero, remainder
// with the sign of the dividend (protoCore's divide / modulo convention).
inline void truncDivMod(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                        const proto::ProtoObject* b,
                        const proto::ProtoObject** quot, const proto::ProtoObject** rem) {
    if (fitsDivisorWord(ctx, b)) {
        if (quot) *quot = a->divide(ctx, b);
        if (rem) *rem = a->modulo(ctx, b);
        return;
    }
    const proto::ProtoObject* zero = ctx->fromLong(0);
    const bool negA = a->compare(ctx, zero) < 0;
    const bool negB = b->compare(ctx, zero) < 0;
    const proto::ProtoObject* x = negA ? a->negate(ctx) : a;
    const proto::ProtoObject* y = negB ? b->negate(ctx) : b;
    TransientPin pinX(ctx, x), pinY(ctx, y);
    // Bit length of the dividend.
    long long bits = 0;
    {
        const proto::ProtoObject* t = x;
        TransientPin pinT(ctx, t);
        while (t->compare(ctx, zero) > 0) {
            t = t->shiftRight(ctx, 32);
            pinT.reset(t);
            bits += 32;
        }
    }
    const proto::ProtoObject* one = ctx->fromLong(1);
    const proto::ProtoObject* q = zero;
    const proto::ProtoObject* r = zero;
    TransientPin pinQ(ctx, q), pinR(ctx, r);
    for (long long i = bits - 1; i >= 0; --i) {
        const proto::ProtoObject* bit = x->shiftRight(ctx, static_cast<int>(i))->bitwiseAnd(ctx, one);
        r = r->shiftLeft(ctx, 1)->add(ctx, bit);
        pinR.reset(r);
        q = q->shiftLeft(ctx, 1);
        if (r->compare(ctx, y) >= 0) {
            r = r->subtract(ctx, y);
            pinR.reset(r);
            q = q->add(ctx, one);
        }
        pinQ.reset(q);
    }
    if (quot) *quot = (negA != negB) ? q->negate(ctx) : q;
    if (rem) *rem = negA ? r->negate(ctx) : r;
}

inline const proto::ProtoObject* integerQuotient(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                                                 const proto::ProtoObject* b) {
    const proto::ProtoObject* q = nullptr;
    truncDivMod(ctx, a, b, &q, nullptr);
    return q;
}

inline const proto::ProtoObject* integerRemainder(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                                                  const proto::ProtoObject* b) {
    const proto::ProtoObject* m = nullptr;
    truncDivMod(ctx, a, b, nullptr, &m);
    return m;
}

} // namespace protoST
