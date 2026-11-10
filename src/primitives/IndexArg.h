#pragma once
// The one reader of a 1-based index argument, shared by every indexing
// primitive: at: and at:put: of Array, OrderedCollection, Interval and
// String, copyFrom:to: of String and ByteArray, ByteArray at: and the packed
// reads (unsignedLongAt:bigEndian: and the others).
//
// An index is any Integer. One beyond a C long long (2^63 or more, or below
// -2^63) is out of the bounds of every collection, so it must signal the same
// SubscriptOutOfBounds as any other out-of-range index; read with asLong it
// raised protoCore's ArithmeticError instead. indexArg answers such an index
// as a value outside every range (LLONG_MAX, or LLONG_MIN for a negative
// one), so each primitive's own bounds check refuses it unchanged, and
// keeps the argument's exact digits for the message (IndexArg::text). An
// argument that is not an Integer signals Error "<who>: the index: an Integer
// was expected", as ArgSupport.h's readers word it.

#include "runtime/NativeError.h"
#include "primitives/ArgSupport.h"
#include "protoCore.h"

#include <climits>
#include <string>

namespace protoST {

struct IndexArg {
    // The index, or LLONG_MAX / LLONG_MIN for an Integer beyond a long long.
    long long value;
    // The decimal digits of the argument as given, sign included.
    std::string text;
};

inline IndexArg indexArg(proto::ProtoContext* ctx, const proto::ProtoObject* v, const char* who) {
    if (!v || v == PROTO_NONE || !v->isInteger(ctx))
        throw NativeError(errorPrefix(who) + "the index: an Integer was expected");
    if (proto::isSmallInt(v)) {
        const long long n = v->asLong(ctx);
        return {n, std::to_string(n)};
    }
    // A LargeInteger. Those that fit a long long are read exactly; the rest
    // lie beyond every collection's bounds.
    std::string digits = v->asIntegerString(ctx, 10)->toStdString(ctx);
    const bool negative = !digits.empty() && digits[0] == '-';
    const std::string magnitude = negative ? digits.substr(1) : digits;
    static const std::string kMax = std::to_string(LLONG_MAX);   // 9223372036854775807
    const bool fits = magnitude.size() < kMax.size()
        || (magnitude.size() == kMax.size() && magnitude <= kMax);
    if (fits) return {std::stoll(digits), std::move(digits)};
    return {negative ? LLONG_MIN : LLONG_MAX, std::move(digits)};
}

} // namespace protoST
