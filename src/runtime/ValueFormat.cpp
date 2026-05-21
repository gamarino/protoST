#include "ValueFormat.h"
#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace protoST {

// --- numeric tower rendering (D11 / D20) -----------------------------------

bool isNumber(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (v == nullptr || v == PROTO_NONE || v == PROTO_TRUE || v == PROTO_FALSE)
        return false;
    if (!ctx) return false;
    try {
        return v->isInteger(ctx) || v->isFloat(ctx);
    } catch (...) {
        return false;
    }
}

namespace {

// Render a Float so it always shows a fractional part and round-trips:
// `4.0` not `4`, `3.14` not `3.140000`. Smalltalk-conventional float syntax.
std::string formatFloat(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    // %.17g round-trips an IEEE double; try progressively shorter forms.
    char buf[64];
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    std::string s(buf);
    // Ensure a fractional part / decimal point is present.
    if (s.find('.') == std::string::npos &&
        s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos &&
        s.find("inf") == std::string::npos &&
        s.find("nan") == std::string::npos) {
        s += ".0";
    }
    return s;
}

// Render an arbitrary-precision integer exactly. protoCore does not expose a
// number -> string conversion, so the exact decimal digits are extracted with
// repeated protoCore `divmod` by 10 — every step stays in protoCore's
// arbitrary-precision integer domain, so a `LargeInteger` far past the 56-bit
// inline range prints exactly (no `asLong` overflow).
std::string formatBigInteger(proto::ProtoContext* ctx,
                             const proto::ProtoObject* v) {
    const proto::ProtoObject* ten  = ctx->fromLong(10);
    const proto::ProtoObject* zero = ctx->fromLong(0);
    const proto::ProtoObject* n    = v;

    bool negative = n->compare(ctx, zero) < 0;
    if (negative) n = n->negate(ctx);

    std::string digits;
    if (n->compare(ctx, zero) == 0) {
        digits = "0";
    } else {
        while (n->compare(ctx, zero) > 0) {
            const proto::ProtoObject* dm = n->divmod(ctx, ten);
            const proto::ProtoTuple*  t  = dm->asTuple(ctx);
            long long d = t->getAt(ctx, 1)->asLong(ctx);
            digits.push_back(static_cast<char>('0' + d));
            n = t->getAt(ctx, 0);
        }
        std::reverse(digits.begin(), digits.end());
    }
    if (negative) digits.insert(digits.begin(), '-');
    return digits;
}

} // anon

std::string formatNumber(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (v->isFloat(ctx)) {
        return formatFloat(v->asDouble(ctx));
    }
    // An integer: a SmallInteger fits a long long; a LargeInteger does not, so
    // render via the protoCore divmod digit extraction either way.
    return formatBigInteger(ctx, v);
}

std::string formatValue(STRuntime& /*rt*/, proto::ProtoContext* ctx,
                        const proto::ProtoObject* v) {
    if (v == nullptr || v == PROTO_NONE) return "nil";
    if (v == PROTO_TRUE)  return "true";
    if (v == PROTO_FALSE) return "false";
    if (!ctx) return "an object";

    // Numbers: SmallInteger, LargeInteger and Float all render exactly.
    if (isNumber(ctx, v)) {
        try {
            return formatNumber(ctx, v);
        } catch (...) {
            // fall through
        }
    }

    // String: print the contents unquoted (matching prior -e / REPL output).
    try {
        const auto* s = v->asString(ctx);
        if (s) return s->toStdString(ctx);
    } catch (...) {
        // not a string — fall through
    }

    // Any other object: replicate the default Object>>printString in C++.
    // (object_prims.cpp prim_Object_printString — kept in sync.)
    static const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");

    // An own `__class_name__` means the value is itself a class object —
    // render its bare name.
    try {
        const proto::ProtoObject* ownName = v->getOwnAttributeDirect(ctx, nameKey);
        if (ownName && ownName != PROTO_NONE) {
            const auto* s = ownName->asString(ctx);
            if (s) return s->toStdString(ctx);
        }
    } catch (...) {
        // fall through
    }

    std::string name;
    try {
        const proto::ProtoObject* nameObj = v->getAttribute(ctx, nameKey);
        if (nameObj && nameObj != PROTO_NONE) {
            const auto* s = nameObj->asString(ctx);
            if (s) name = s->toStdString(ctx);
        }
    } catch (...) {
        // fall through
    }
    if (name.empty()) return "an object";

    char c0 = name[0];
    bool vowel = (c0 == 'A' || c0 == 'E' || c0 == 'I' || c0 == 'O' || c0 == 'U');
    return (vowel ? "an " : "a ") + name;
}

} // namespace protoST
