#pragma once
// The argument checks shared by the primitives. Each takes the value a
// primitive received, signals an Error that names the operation (`who`, the
// public selector, through errorPrefix) and the argument (`what`), and
// answers the C++ value. There is one copy of each
// check: a primitive that needs one includes this header instead of keeping
// its own variant.

#include "primitives/BytesSupport.h"
#include "runtime/NativeError.h"
#include "protoCore.h"

#include <climits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace protoST {

// The start of an error text about the operation `who`: "who: ", or "who "
// when `who` is a keyword selector, which already ends in a colon
// ("Future class>>after: the milliseconds ...", never "after:: ...").
inline std::string errorPrefix(std::string_view who) {
    std::string out(who);
    out += (!out.empty() && out.back() == ':') ? " " : ": ";
    return out;
}

// A SmallInteger. Anything that is not an Integer signals
// "<who>: <what>: an Integer was expected"; a LargeInteger is an Integer, but
// no primitive taking this argument accepts one, so it signals
// "<who>: <what> is out of range".
inline long long smallIntegerArg(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                                 const char* who, const char* what) {
    if (!v || v == PROTO_NONE || !v->isInteger(ctx))
        throw NativeError(errorPrefix(who) + what + ": an Integer was expected");
    if (!proto::isSmallInt(v))
        throw NativeError(errorPrefix(who) + what + " is out of range");
    return v->asLong(ctx);
}

// A SmallInteger that fits a C int (a descriptor, a port, a pid, a signal, a
// timeout in milliseconds): never narrowed silently.
inline int intArg(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                  const char* who, const char* what) {
    const long long n = smallIntegerArg(ctx, v, who, what);
    if (n < INT_MIN || n > INT_MAX)
        throw NativeError(errorPrefix(who) + what + " is out of range");
    return static_cast<int>(n);
}

// true or false; anything else signals "<who>: <what> must be true or false".
inline bool booleanArg(const proto::ProtoObject* v, const char* who, const char* what) {
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    throw NativeError(errorPrefix(who) + what + " must be true or false");
}

// The contents of a ByteArray argument, read-only (bytesView: the view stays
// valid while `v` is reachable); anything else signals
// "<who>: a ByteArray was expected".
inline std::string_view bytesArg(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                                 const char* who) {
    if (!v || v == PROTO_NONE || !v->isByteBuffer(ctx))
        throw NativeError(errorPrefix(who) + "a ByteArray was expected");
    return bytesView(ctx, v);
}

// A String handed to the operating system: a path, an argv element, a shell
// command line, an environment name or value, a host name, a module name.
// The OS reads such a string only up to its first NUL, so a protoST String
// holding one (a String may contain U+0000) would name something else:
// 'victim', NUL, '.evil' would open 'victim'. It is refused, as CPython
// refuses it with ValueError: "<who>: <what> must not contain NUL". Anything
// that is not a String signals "<who>: a String was expected".
//
// protoIO, which makes the system calls, does not check this itself (a
// follow-up for protoIO 0.3.1, KNOWN_ISSUES.md): every primitive that passes
// a string to it as one of the above reads it through this function.
inline std::string osStringArg(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                               const char* who, const char* what) {
    const proto::ProtoString* s = (v && v != PROTO_NONE) ? v->asString(ctx) : nullptr;
    if (!s) throw NativeError(errorPrefix(who) + "a String was expected");
    std::string out = s->toStdString(ctx);
    if (out.find('\0') != std::string::npos)
        throw NativeError(errorPrefix(who) + what + " must not contain NUL");
    return out;
}

} // namespace protoST
