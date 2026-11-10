#pragma once
// The one reader of a 1-based index argument, shared by every indexing
// primitive: at: of Array, OrderedCollection, Interval and String; at:put: of
// Array and OrderedCollection; copyFrom:to: of String and ByteArray;
// ByteArray at: and the packed reads (unsignedLongAt:bigEndian: and the
// others). The indexing methods written in Smalltalk apply the same rule
// through Object>>__checkIndex:between:and:for: (lib/kernel/collection.st).
//
// An index is any Integer. One beyond a C long long (2^63 or more, or below
// -2^63) is out of the bounds of every collection, so it must signal the same
// SubscriptOutOfBounds as any other out-of-range index; read with asLong it
// raised protoCore's ArithmeticError instead. indexArg answers such an index
// as a value outside every range (LLONG_MAX, or LLONG_MIN for a negative
// one), so each primitive's own bounds check refuses it unchanged, and
// keeps the argument, whose exact digits IndexArg::text builds for the
// message only. An argument that is not an Integer (a Character included,
// which asLong would read as its code point) signals Error "<who>: the index:
// an Integer was expected", as ArgSupport.h's readers word it.

#include "runtime/NativeError.h"
#include "primitives/ArgSupport.h"
#include "protoCore.h"

#include <climits>
#include <string>

namespace protoST {

// The decimal digits of an Integer, sign included.
inline std::string integerDigits(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (proto::isSmallInt(v)) return std::to_string(v->asLong(ctx));
    return v->asIntegerString(ctx, 10)->toStdString(ctx);
}

struct IndexArg {
    // The index, or LLONG_MAX / LLONG_MIN for an Integer beyond a long long.
    long long value;
    // The argument as given. It is an argument of the primitive, so the
    // caller's frame keeps it alive.
    const proto::ProtoObject* argument;

    // The decimal digits of the argument as given, sign included. Built only
    // when an error message needs them, never on the successful path.
    std::string text(proto::ProtoContext* ctx) const { return integerDigits(ctx, argument); }
};

inline IndexArg indexArg(proto::ProtoContext* ctx, const proto::ProtoObject* v, const char* who) {
    if (!v || v == PROTO_NONE || !v->isInteger(ctx))
        throw NativeError(errorPrefix(who) + "the index: an Integer was expected");
    if (proto::isSmallInt(v)) return {v->asLong(ctx), v};
    // A LargeInteger (rare as an index: beyond 2^55 in magnitude). Those that
    // fit a long long are read exactly; the rest lie beyond every
    // collection's bounds.
    const std::string digits = integerDigits(ctx, v);
    const bool negative = !digits.empty() && digits[0] == '-';
    const std::string magnitude = negative ? digits.substr(1) : digits;
    static const std::string kMax = std::to_string(LLONG_MAX);   // 9223372036854775807
    const bool fits = magnitude.size() < kMax.size()
        || (magnitude.size() == kMax.size() && magnitude <= kMax);
    if (fits) return {std::stoll(digits), v};
    return {negative ? LLONG_MIN : LLONG_MAX, v};
}

} // namespace protoST
