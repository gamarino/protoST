// Correctly rounded sums of Floats, class-side on `Float` (0.15.0):
//
//   Float fsum: aSequence                          CPython's math.fsum
//   Float fsumOfProducts: a with: b                fsum of a_i * b_i
//   Float fsumOfSquaredDifferences: a with: b      fsum of (a_i - b_i) * (a_i - b_i)
//   Float fsumOfPowSquaredDifferences: a with: b   fsum of pow(a_i - b_i, 2.0)    (0.16.0)
//
// Specified by the protoBaseST vectors addendum (2026-11-19, section 2.2.4),
// whose Smalltalk kernel is the reference and the fallback. The algorithm is a
// line-for-line port of `math_fsum` in CPython's Modules/mathmodule.c
// (Shewchuk's partials, the special-value path and the final half-way
// correction), so the answer is CPython's to the last bit, NaN, the
// infinities and -0.0 included. tests/conformance/12-builtins/gen_fsum_vectors.py
// generates the vectors from CPython itself.
//
// Floating-point contract. Shewchuk's error-free transformations need every
// `+`, `-` and `*` below to be one IEEE double operation, rounded to nearest:
// a fused multiply-add would make `lo` wrong and would also change the
// rounded products of the two-sequence forms. This translation unit is
// therefore compiled with -ffp-contract=off on GCC and Clang (CMakeLists.txt);
// MSVC (VS 2022) does not contract under its default /fp:precise. Nothing here
// may call std::fma. The x86-64 and arm64 targets protoST supports evaluate
// doubles in double precision (FLT_EVAL_METHOD 0), as CPython requires.
//
// Elements. Every element must be a Float: an Integer (or a Fraction, a
// Decimal, anything else) is refused with an Error, so that every conversion
// stays explicit in Smalltalk. protoBaseST converts with its own floatOf:.
//
// Sequences. An Array or an OrderedCollection (or a subclass of either) is read
// directly from its element list, a snapshot taken once. Any other object is
// read through the messages `size` and `at:` (1 to size), so a Smalltalk
// sequence such as protoBaseST's PBTuple or PBFrozenList is accepted as it is,
// at the cost of one send per element. An Interval, a String or a Bag answer
// size/at: (or do not) with their own semantics; their elements are refused
// unless they are Floats.
//
// Errors (Error, the text as below):
//   'fsum: intermediate overflow'   CPython's OverflowError
//   'fsum: -inf + inf'              CPython's ValueError
//   '<selector> the sequences differ in size (m and n)'
//   'fsum: element i is not a Float'
//   '<selector> element i of the <first|second> sequence is not a Float'
//   '<selector> the argument does not answer size and at: as a sequence'
// The elements of both sequences are read and checked before any summing, so
// a refused element is reported even when an overflow would have come first.
//
// The pow form. Its terms are what protoBaseST's kernel sums with
// `(a_i - b_i) raisedTo: 2` and proto_db with `(x - y) ** 2`: the C library's
// pow, which on glibc is not always d * d (see powSquareExponent below). The
// answer is therefore the platform's, as CPython's is. One difference from
// CPython is kept: a square that overflows is an infinite term here, as
// raisedTo: answers, where CPython's `**` raises OverflowError before fsum.

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "primitives/ArgSupport.h"
#include "primitives/CollectionSupport.h"
#include "runtime/Bootstrap.h"
#include "runtime/NativeError.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace protoST {

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

namespace {

using PO = proto::ProtoObject;

// One element as a double, or an Error naming its position.
double floatElement(proto::ProtoContext* ctx, const PO* v, long long index,
                    const char* who, const char* which) {
    if (!v || v == PROTO_NONE || !v->isFloat(ctx)) {
        throw NativeError(errorPrefix(who) + "element " + std::to_string(index)
                          + (which ? std::string(" of the ") + which + " sequence" : std::string())
                          + " is not a Float");
    }
    return v->asDouble(ctx);
}

// The Floats of `seq`, in order, into `out`.
void readFloats(STRuntime& rt, proto::ProtoContext* ctx, const PO* seq,
                const char* who, const char* which, std::vector<double>& out) {
    out.clear();
    if (isArrayOrOrdered(rt, ctx, seq)) {
        // The element list is immutable: another actor may replace the
        // collection's list, never change this one. It is pinned while it is
        // read, although reading a Float's double allocates nothing.
        const proto::ProtoList* data = arrayOrOrderedElements(rt, ctx, seq);
        TransientPin pin(ctx, asRootable(data));
        const auto n = static_cast<long long>(data->getSize(ctx));
        out.reserve(static_cast<size_t>(n));
        for (long long i = 0; i < n; ++i)
            out.push_back(floatElement(ctx, data->getAt(ctx, static_cast<int>(i)), i + 1, who, which));
        return;
    }
    // Any other sequence: size, then at: 1 .. size. The sequence is an
    // argument of the primitive, so the caller's frame keeps it alive; each
    // element is read into a double before the next send.
    bool understood = false;
    const PO* sizeObj = sendDynamic(rt, ctx, seq, proto::ProtoString::createSymbol(ctx, "size"),
                                    nullptr, 0, &understood);
    if (!understood || !sizeObj || !proto::isSmallInt(sizeObj) || sizeObj->asLong(ctx) < 0)
        throw NativeError(errorPrefix(who) + "the argument does not answer size and at: as a sequence");
    const long long n = sizeObj->asLong(ctx);
    const proto::ProtoString* atSel = proto::ProtoString::createSymbol(ctx, "at:");
    out.reserve(static_cast<size_t>(n));
    for (long long i = 1; i <= n; ++i) {
        const PO* index = ctx->fromLong(i);   // a SmallInteger: no cell
        understood = false;
        const PO* v = sendDynamic(rt, ctx, seq, atSel, &index, 1, &understood);
        if (!understood)
            throw NativeError(errorPrefix(who) + "the argument does not answer size and at: as a sequence");
        out.push_back(floatElement(ctx, v, i, who, which));
    }
}

// CPython's math_fsum (Modules/mathmodule.c, 3.14), over the terms produced by
// `term(k)` for k in [0, n). Kept statement for statement; the partials live
// in a std::vector instead of the stack array plus _fsum_realloc.
template <typename Term>
double cpythonFsum(size_t n_terms, Term term) {
    std::vector<double> p;
    p.reserve(32);
    size_t i, j, n = 0;
    double x, y, t;
    double xsave, special_sum = 0.0, inf_sum = 0.0;
    double hi, yr, lo = 0.0;

    for (size_t k = 0; k < n_terms; ++k) {      /* for x in iterable */
        x = term(k);
        xsave = x;
        for (i = j = 0; j < n; j++) {           /* for y in partials */
            y = p[j];
            if (std::fabs(x) < std::fabs(y)) {
                t = x; x = y; y = t;
            }
            hi = x + y;
            yr = hi - x;
            lo = y - yr;
            if (lo != 0.0)
                p[i++] = lo;
            x = hi;
        }

        n = i;                                  /* ps[i:] = [x] */
        if (x != 0.0) {
            if (!std::isfinite(x)) {
                /* a nonfinite x could arise either as a result of
                   intermediate overflow, or as a result of a nan or inf
                   in the summands */
                if (std::isfinite(xsave))
                    throw NativeError("fsum: intermediate overflow");
                if (std::isinf(xsave))
                    inf_sum += xsave;
                special_sum += xsave;
                /* reset partials */
                n = 0;
            } else {
                if (n >= p.size()) p.resize(n + 1);
                p[n++] = x;
            }
        }
    }

    if (special_sum != 0.0) {                   /* NaN != 0.0 too */
        if (std::isnan(inf_sum))
            throw NativeError("fsum: -inf + inf");
        return special_sum;
    }

    hi = 0.0;
    if (n > 0) {
        hi = p[--n];
        /* sum_exact(ps, hi) from the top, stop when the sum becomes
           inexact. */
        while (n > 0) {
            x = hi;
            y = p[--n];
            hi = x + y;
            yr = hi - x;
            lo = y - yr;
            if (lo != 0.0)
                break;
        }
        /* Make half-even rounding work across multiple partials (so that
           fsum([1e-16, 1, 1e16]) rounds the last digit up to two). */
        if (n > 0 && ((lo < 0.0 && p[n-1] < 0.0) ||
                      (lo > 0.0 && p[n-1] > 0.0))) {
            y = lo * 2.0;
            x = hi + y;
            yr = x - hi;
            if (y == yr)
                hi = x;
        }
    }
    return hi;
}

// The two Float sequences of a two-argument form, of equal size.
void readPair(STRuntime& rt, proto::ProtoContext* ctx, const PO* const* a, const char* who,
              std::vector<double>& xs, std::vector<double>& ys) {
    readFloats(rt, ctx, a[0], who, "first", xs);
    readFloats(rt, ctx, a[1], who, "second", ys);
    if (xs.size() != ys.size())
        throw NativeError(errorPrefix(who) + "the sequences differ in size ("
                          + std::to_string(xs.size()) + " and " + std::to_string(ys.size()) + ")");
}

// Float fsum: aSequence
const PO* prim_Fsum(STRuntime& rt, proto::ProtoContext* ctx, const PO*,
                    const PO* const* a, int) {
    std::vector<double> xs;
    readFloats(rt, ctx, a[0], "fsum:", nullptr, xs);
    return ctx->fromDouble(cpythonFsum(xs.size(), [&](size_t k) { return xs[k]; }));
}

// Float fsumOfProducts: a with: b — each term the IEEE product, rounded once.
const PO* prim_FsumOfProducts(STRuntime& rt, proto::ProtoContext* ctx, const PO*,
                              const PO* const* a, int) {
    std::vector<double> xs, ys;
    readPair(rt, ctx, a, "fsumOfProducts:with:", xs, ys);
    return ctx->fromDouble(cpythonFsum(xs.size(), [&](size_t k) {
        const double prod = xs[k] * ys[k];
        return prod;
    }));
}

// Float fsumOfSquaredDifferences: a with: b — the difference rounded, then
// the product of the rounded difference with itself rounded.
const PO* prim_FsumOfSquaredDifferences(STRuntime& rt, proto::ProtoContext* ctx, const PO*,
                                        const PO* const* a, int) {
    std::vector<double> xs, ys;
    readPair(rt, ctx, a, "fsumOfSquaredDifferences:with:", xs, ys);
    return ctx->fromDouble(cpythonFsum(xs.size(), [&](size_t k) {
        const double d = xs[k] - ys[k];
        const double sq = d * d;
        return sq;
    }));
}

// The exponent of fsumOfPowSquaredDifferences:with:, read through a volatile
// so that it reaches pow as a run-time value. GCC and Clang fold
// pow(x, 2.0) with a constant 2.0 into x * x (even at -O2, without
// -ffast-math), and glibc's pow is not correctly rounded: when the exact
// square lies half-way between two doubles, or close to it, pow(x, 2.0) and
// x * x differ in the last bit. CPython's `x ** 2` and protoST's
// `x raisedTo: 2` call pow with the exponent as data, so this must too. The
// objdump check in the 0.16.0 report and the decisive cases of
// tests/conformance/12-builtins/_vectors_fsum_pow.st both catch a fold.
volatile double powSquareExponent = 2.0;

// Float fsumOfPowSquaredDifferences: a with: b (0.16.0) — each term
// pow(d, 2.0), d = a_i - b_i rounded first: the C library's pow, as
// `d raisedTo: 2` and CPython's `d ** 2` compute it, and not d * d.
const PO* prim_FsumOfPowSquaredDifferences(STRuntime& rt, proto::ProtoContext* ctx, const PO*,
                                           const PO* const* a, int) {
    std::vector<double> xs, ys;
    readPair(rt, ctx, a, "fsumOfPowSquaredDifferences:with:", xs, ys);
    const double exponent = powSquareExponent;   // one volatile read per call
    return ctx->fromDouble(cpythonFsum(xs.size(), [&](size_t k) {
        const double d = xs[k] - ys[k];
        return std::pow(d, exponent);
    }));
}

} // namespace

void installFsumPrimitives(STRuntime& rt) {
    auto* F = rt.bootstrap().floatProto;
    bindClassPrimitive(rt, F, "fsum:",                          primitiveMethod<prim_Fsum>);
    bindClassPrimitive(rt, F, "fsumOfProducts:with:",           primitiveMethod<prim_FsumOfProducts>);
    bindClassPrimitive(rt, F, "fsumOfSquaredDifferences:with:", primitiveMethod<prim_FsumOfSquaredDifferences>);
    bindClassPrimitive(rt, F, "fsumOfPowSquaredDifferences:with:", primitiveMethod<prim_FsumOfPowSquaredDifferences>);
}

} // namespace protoST
