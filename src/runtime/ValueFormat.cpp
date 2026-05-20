#include "ValueFormat.h"
#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <string>

namespace protoST {

std::string formatValue(STRuntime& /*rt*/, proto::ProtoContext* ctx,
                        const proto::ProtoObject* v) {
    if (v == nullptr || v == PROTO_NONE) return "nil";
    if (v == PROTO_TRUE)  return "true";
    if (v == PROTO_FALSE) return "false";
    if (!ctx) return "an object";

    // Integer: print the decimal digits.
    try {
        return std::to_string(v->asLong(ctx));
    } catch (...) {
        // not an integer — fall through
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
