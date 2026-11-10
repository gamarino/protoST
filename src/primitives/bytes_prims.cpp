#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "primitives/BytesSupport.h"
#include "runtime/Bootstrap.h"
#include "runtime/ClassedErrorSignal.h"
#include "protoCore.h"

#include <protoio/encoding.h>
#include <protoio/error.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace protoST {

// ByteArray (docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md,
// section 4.1): an immutable value over protoCore's ProtoByteBuffer.
//
// A ProtoByteBuffer is contiguous, opaque storage for octets 0-255, NUL safe.
// protoST uses it as a value: the buffer is filled once, by makeBytes, and
// never written afterwards (ProtoByteBuffer::setAt is never called), so a
// ByteArray can be shared between actors, kept in a Dictionary and hashed
// without a lock. Every operation that "changes" bytes answers a new value.
//
// The primitives below are bound on the ByteArray class prototype and are
// reached through the protocol in lib/kernel/bytes.st. A ProtoByteBuffer
// value dispatches to that prototype (Bootstrap.cpp: the space's
// bufferPrototype slot, and methodHolder).
//
// protoCore's own hash of a byte buffer is its identity, so content equality
// and the content hash are computed here.

const proto::ProtoObject* makeBytes(proto::ProtoContext* ctx, std::string_view bytes) {
    return ctx->newByteBuffer(bytes.data(), static_cast<proto::proto_ulong>(bytes.size()))
        ->asObject(ctx);
}

std::string_view bytesView(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const proto::ProtoByteBuffer* b =
        (v && v != PROTO_NONE) ? v->asByteBuffer(ctx) : nullptr;
    if (!b) throw std::runtime_error("a ByteArray was expected");
    const proto::proto_ulong n = b->getSize(ctx);
    if (n == 0) return {};
    return std::string_view(b->getBuffer(ctx), static_cast<size_t>(n));
}

namespace {

using PO = proto::ProtoObject;

#define PRIM(NAME) const PO* NAME(STRuntime& rt, proto::ProtoContext* ctx, const PO* r, \
                                   const PO* const* a, int argc)
#define ARGS(N, SEL) if (argc != (N)) throw std::runtime_error(SEL " expects " #N " argument(s)"); \
                     (void) rt; (void) r; (void) a

long long integerArg(proto::ProtoContext* ctx, const PO* v, const char* who) {
    if (!v || v == PROTO_NONE || !v->isInteger(ctx) || !proto::isSmallInt(v))
        throw std::runtime_error(std::string(who) + ": an Integer was expected");
    return v->asLong(ctx);
}

// The byte an element of `withAll:` stands for: an Integer from 0 to 255.
char byteOf(proto::ProtoContext* ctx, const PO* v) {
    if (!v || v == PROTO_NONE || !v->isInteger(ctx) || !proto::isSmallInt(v))
        throw std::runtime_error("ByteArray withAll: each element must be an Integer from 0 to 255");
    const long long b = v->asLong(ctx);
    if (b < 0 || b > 255)
        throw std::runtime_error("ByteArray withAll: " + std::to_string(b)
                                 + " is not a byte; each element must be an Integer from 0 to 255");
    return static_cast<char>(b);
}

// FNV-1a, 64 bits. The hashed collections take a hash in [0, 2^53), so the
// top eleven bits are folded into the low ones and the result is masked.
// Deterministic: equal contents hash alike in every run.
long long contentHash(std::string_view bytes) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ull;
    }
    constexpr std::uint64_t mask = (std::uint64_t{1} << 53) - 1;
    return static_cast<long long>((h ^ (h >> 53)) & mask);
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ByteArray __bytesNew: n -> n zero bytes.
PRIM(prim_BytesNew) {
    ARGS(1, "__bytesNew:");
    const long long n = integerArg(ctx, a[0], "ByteArray new:");
    if (n < 0) throw std::runtime_error("ByteArray new: the size must not be negative");
    return makeBytes(ctx, std::string(static_cast<size_t>(n), '\0'));
}

// ByteArray __bytesWithAll: anArray -> the bytes the Array's Integers name.
PRIM(prim_BytesWithAll) {
    ARGS(1, "__bytesWithAll:");
    const PO* data = (a[0] && a[0] != PROTO_NONE)
        ? a[0]->getAttribute(ctx, rt.bootstrap().sym.data)
        : nullptr;
    const proto::ProtoList* list =
        (data && data != PROTO_NONE) ? data->asList(ctx) : nullptr;
    if (!list) throw std::runtime_error("ByteArray withAll: an Array was expected");
    const proto::proto_ulong n = list->getSize(ctx);
    std::string out;
    out.reserve(static_cast<size_t>(n));
    for (proto::proto_ulong i = 0; i < n; ++i)
        out.push_back(byteOf(ctx, list->getAt(ctx, static_cast<int>(i))));
    return makeBytes(ctx, out);
}

PRIM(prim_BytesSize) {
    ARGS(0, "__bytesSize");
    return ctx->fromLong(static_cast<long long>(bytesView(ctx, r).size()));
}

// aByteArray __bytesAt: i -> the Integer 0-255 at the 1-based index i.
PRIM(prim_BytesAt) {
    ARGS(1, "__bytesAt:");
    const std::string_view v = bytesView(ctx, r);
    const long long i = integerArg(ctx, a[0], "ByteArray at:");
    if (i < 1 || i > static_cast<long long>(v.size()))
        throw ClassedErrorSignal("SubscriptOutOfBounds", "ByteArray>>at: index " + std::to_string(i)
                                 + " out of bounds 1.." + std::to_string(v.size()));
    return ctx->fromLong(static_cast<unsigned char>(v[static_cast<size_t>(i - 1)]));
}

// aByteArray __bytesCopyFrom: i to: j -> a new value; empty when j < i, as
// for String.
PRIM(prim_BytesCopyFromTo) {
    ARGS(2, "__bytesCopyFrom:to:");
    const std::string_view v = bytesView(ctx, r);
    const long long from = integerArg(ctx, a[0], "ByteArray copyFrom:to:");
    const long long to = integerArg(ctx, a[1], "ByteArray copyFrom:to:");
    if (to < from) return makeBytes(ctx, {});
    if (from < 1 || to > static_cast<long long>(v.size()))
        throw ClassedErrorSignal("SubscriptOutOfBounds", "ByteArray>>copyFrom:to: range "
                                 + std::to_string(from) + " to " + std::to_string(to)
                                 + " out of bounds (size " + std::to_string(v.size()) + ")");
    return makeBytes(ctx, v.substr(static_cast<size_t>(from - 1), static_cast<size_t>(to - from + 1)));
}

PRIM(prim_BytesConcat) {
    ARGS(1, "__bytesConcat:");
    std::string out(bytesView(ctx, r));
    out.append(bytesView(ctx, a[0]));
    return makeBytes(ctx, out);
}

// Equal contents; false (not an error) for anything that is not a ByteArray.
PRIM(prim_BytesEquals) {
    ARGS(1, "__bytesEquals:");
    if (r == a[0]) return PROTO_TRUE;
    if (!a[0] || a[0] == PROTO_NONE || !a[0]->isByteBuffer(ctx)) return PROTO_FALSE;
    return bytesView(ctx, r) == bytesView(ctx, a[0]) ? PROTO_TRUE : PROTO_FALSE;
}

PRIM(prim_BytesHash) {
    ARGS(0, "__bytesHash");
    return ctx->fromLong(contentHash(bytesView(ctx, r)));
}

// Lowercase hexadecimal, two digits per byte.
PRIM(prim_BytesHex) {
    ARGS(0, "__bytesHex");
    static const char digits[] = "0123456789abcdef";
    const std::string_view v = bytesView(ctx, r);
    std::string out;
    out.reserve(v.size() * 2);
    for (unsigned char c : v) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0xF]);
    }
    return ctx->fromUTF8String(out.c_str());
}

// ByteArray __fromHex: aString -> the bytes it spells, two hexadecimal
// digits per byte, in either case.
PRIM(prim_BytesFromHex) {
    ARGS(1, "__fromHex:");
    const proto::ProtoString* s =
        (a[0] && a[0] != PROTO_NONE) ? a[0]->asString(ctx) : nullptr;
    if (!s) throw std::runtime_error("ByteArray fromHex: a String was expected");
    const std::string hex = s->toStdString(ctx);
    if (hex.size() % 2 != 0)
        throw std::runtime_error("ByteArray fromHex: the String must have an even number of digits");
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexDigit(hex[i]), lo = hexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0)
            throw std::runtime_error("ByteArray fromHex: '" + hex.substr(i, 2)
                                     + "' at position " + std::to_string(i + 1)
                                     + " is not a pair of hexadecimal digits");
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return makeBytes(ctx, out);
}

// The length of the well-formed UTF-8 sequence at the start of `v` (RFC 3629
// as Unicode defines it: no overlong forms, no surrogates, nothing above
// U+10FFFF), or 0 when the bytes there are not one.
size_t utf8SequenceLength(std::string_view v) {
    const auto at = [&](size_t i) { return static_cast<unsigned char>(v[i]); };
    const unsigned char b0 = at(0);
    if (b0 < 0x80) return 1;
    size_t len;
    unsigned char lo = 0x80, hi = 0xBF;  // the permitted range of the second byte
    if (b0 >= 0xC2 && b0 <= 0xDF) len = 2;
    else if (b0 >= 0xE0 && b0 <= 0xEF) {
        len = 3;
        if (b0 == 0xE0) lo = 0xA0;       // else overlong
        if (b0 == 0xED) hi = 0x9F;       // else a surrogate
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        len = 4;
        if (b0 == 0xF0) lo = 0x90;       // else overlong
        if (b0 == 0xF4) hi = 0x8F;       // else above U+10FFFF
    } else return 0;                      // a continuation byte, 0xC0, 0xC1, 0xF5..0xFF
    if (v.size() < len || at(1) < lo || at(1) > hi) return 0;
    for (size_t i = 2; i < len; ++i)
        if ((at(i) & 0xC0) != 0x80) return 0;
    return len;
}

// ByteArray __utf8Encode: aString -> the UTF-8 bytes of aString. Built from
// the whole String (data and length), so an embedded NUL survives.
PRIM(prim_Utf8Encode) {
    ARGS(1, "__utf8Encode:");
    const proto::ProtoString* s =
        (a[0] && a[0] != PROTO_NONE) ? a[0]->asString(ctx) : nullptr;
    if (!s) throw std::runtime_error("utf8Encoded: a String was expected");
    return makeBytes(ctx, s->toStdString(ctx));
}

// aByteArray __utf8Decode -> the String the bytes spell. Strict: the first
// ill-formed sequence signals Error, naming its 1-based position.
PRIM(prim_Utf8Decode) {
    ARGS(0, "__utf8Decode");
    const std::string_view v = bytesView(ctx, r);
    for (size_t i = 0; i < v.size();) {
        const size_t n = utf8SequenceLength(v.substr(i));
        if (n == 0)
            throw std::runtime_error("utf8Decoded: the bytes are not valid UTF-8 (byte "
                                     + std::to_string(static_cast<unsigned>(static_cast<unsigned char>(v[i])))
                                     + " at position " + std::to_string(i + 1) + ")");
        i += n;
    }
    // fromUTF8Buffer takes an explicit length, so a NUL does not end the text.
    std::uint8_t remainder[4];
    std::uint8_t remainderCount = 0;
    return proto::ProtoString::fromUTF8Buffer(
               ctx, reinterpret_cast<const std::uint8_t*>(v.data()), v.size(),
               nullptr, 0, remainder, &remainderCount)
        ->asObject(ctx);
}

// ByteArray __bytesJoin: anArray -> one new value holding the contents of
// each ByteArray of anArray in order. WriteStream's contents uses it.
PRIM(prim_BytesJoin) {
    ARGS(1, "__bytesJoin:");
    const PO* data = (a[0] && a[0] != PROTO_NONE)
        ? a[0]->getAttribute(ctx, rt.bootstrap().sym.data)
        : nullptr;
    const proto::ProtoList* list =
        (data && data != PROTO_NONE) ? data->asList(ctx) : nullptr;
    if (!list) throw std::runtime_error("ByteArray join: an Array was expected");
    const proto::proto_ulong n = list->getSize(ctx);
    std::string out;
    for (proto::proto_ulong i = 0; i < n; ++i)
        out.append(bytesView(ctx, list->getAt(ctx, static_cast<int>(i))));
    return makeBytes(ctx, out);
}


// ---- numbers in bytes (section 4.1: packing) -------------------------------
//
// Fixed-width integers and IEEE 754 floats are packed and read by
// protoio::encoding, which is byte-compatible with CPython's struct module
// (the shared vectors in ../protoIO/tests/vectors/packing.json). The
// primitives only convert between protoST values and C++ ones, and turn
// protoio's InvalidArgument into an Error that names the problem.
//
// Integers come in as any SmallInteger or LargeInteger: the decimal digits are
// read exactly and range-checked against 64 bits here, so a value that does
// not fit signals Error and nothing wraps. A 64-bit field read back above
// 2^55 answers a LargeInteger. Floats go through their bit patterns, so a NaN
// payload and -0.0 survive; a Float32 field rounds a finite Float to the
// nearest single precision value and refuses one beyond its range.

// The exact decimal text of an Integer (sign included); throws unless `v` is an Integer.
std::string integerDigits(proto::ProtoContext* ctx, const PO* v, const char* who) {
    if (!v || v == PROTO_NONE || !v->isInteger(ctx))
        throw std::runtime_error(std::string(who) + ": an Integer was expected");
    return v->asIntegerString(ctx, 10)->toStdString(ctx);
}

const PO* fromUnsigned64(proto::ProtoContext* ctx, std::uint64_t u) {
    if (u <= static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
        return ctx->fromLong(static_cast<long long>(u));
    return ctx->fromString(std::to_string(u).c_str(), 10);
}

bool booleanArg(const PO* v, const char* who) {
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    throw std::runtime_error(std::string(who) + ": bigEndian must be true or false");
}

int widthArg(proto::ProtoContext* ctx, const PO* v, const char* who) {
    const long long w = integerArg(ctx, v, who);
    if (w != 1 && w != 2 && w != 4 && w != 8)
        throw std::runtime_error(std::string(who) + ": the width must be 1, 2, 4 or 8, not "
                                 + std::to_string(w));
    return static_cast<int>(w);
}

// Runs a protoio call, answering its failure as an Error with the primitive's name.
template <class F>
auto packing(const char* who, F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const protoio::Error& e) {
        throw std::runtime_error(std::string(who) + ": " + e.what());
    }
}

// The 0-based offset of the 1-based `index`, checked so that `width` bytes
// are there; reading past the end signals SubscriptOutOfBounds.
size_t offsetOf(proto::ProtoContext* ctx, const PO* indexArg, int width, size_t size,
                const char* who) {
    const long long i = integerArg(ctx, indexArg, who);
    if (i < 1 || static_cast<unsigned long long>(i) > size
        || size - static_cast<size_t>(i - 1) < static_cast<size_t>(width))
        throw ClassedErrorSignal("SubscriptOutOfBounds", std::string(who) + ": reading "
                                 + std::to_string(width) + " byte(s) at index " + std::to_string(i)
                                 + " is out of bounds (size " + std::to_string(size) + ")");
    return static_cast<size_t>(i - 1);
}

// aByteArray __unpackUnsignedAt: i width: w bigEndian: b
PRIM(prim_UnpackUnsigned) {
    ARGS(3, "__unpackUnsignedAt:width:bigEndian:");
    const std::string_view v = bytesView(ctx, r);
    const int w = widthArg(ctx, a[1], "ByteArray unsigned read");
    const size_t off = offsetOf(ctx, a[0], w, v.size(), "ByteArray unsigned read");
    const bool big = booleanArg(a[2], "ByteArray unsigned read");
    return fromUnsigned64(ctx, packing("ByteArray unsigned read",
                                       [&] { return protoio::encoding::getUint(v, off, w, big); }));
}

PRIM(prim_UnpackSigned) {
    ARGS(3, "__unpackSignedAt:width:bigEndian:");
    const std::string_view v = bytesView(ctx, r);
    const int w = widthArg(ctx, a[1], "ByteArray signed read");
    const size_t off = offsetOf(ctx, a[0], w, v.size(), "ByteArray signed read");
    const bool big = booleanArg(a[2], "ByteArray signed read");
    return ctx->fromLong(packing("ByteArray signed read",
                                 [&] { return protoio::encoding::getInt(v, off, w, big); }));
}

// aByteArray __unpackFloatAt: i width: 4|8 bigEndian: b -> a Float.
PRIM(prim_UnpackFloat) {
    ARGS(3, "__unpackFloatAt:width:bigEndian:");
    const std::string_view v = bytesView(ctx, r);
    const int w = widthArg(ctx, a[1], "ByteArray float read");
    if (w != 4 && w != 8) throw std::runtime_error("ByteArray float read: the width must be 4 or 8");
    const size_t off = offsetOf(ctx, a[0], w, v.size(), "ByteArray float read");
    const bool big = booleanArg(a[2], "ByteArray float read");
    return ctx->fromDouble(packing("ByteArray float read", [&] {
        return w == 4 ? static_cast<double>(protoio::encoding::getFloat32(v, off, big))
                      : protoio::encoding::getFloat64(v, off, big);
    }));
}

// ByteArray __packUnsigned: n width: w bigEndian: b -> the w bytes of n.
PRIM(prim_PackUnsigned) {
    ARGS(3, "__packUnsigned:width:bigEndian:");
    const std::string text = integerDigits(ctx, a[0], "ByteArray unsigned write");
    const int w = widthArg(ctx, a[1], "ByteArray unsigned write");
    const bool big = booleanArg(a[2], "ByteArray unsigned write");
    errno = 0;
    char* end = nullptr;
    const std::uint64_t u = text[0] == '-' ? 0 : std::strtoull(text.c_str(), &end, 10);
    if (text[0] == '-' || errno == ERANGE)
        throw std::runtime_error("ByteArray unsigned write: " + text + " does not fit in "
                                 + std::to_string(w) + " unsigned byte(s)");
    std::string out;
    packing("ByteArray unsigned write", [&] { protoio::encoding::putUint(out, u, w, big); return 0; });
    return makeBytes(ctx, out);
}

PRIM(prim_PackSigned) {
    ARGS(3, "__packSigned:width:bigEndian:");
    const std::string text = integerDigits(ctx, a[0], "ByteArray signed write");
    const int w = widthArg(ctx, a[1], "ByteArray signed write");
    const bool big = booleanArg(a[2], "ByteArray signed write");
    errno = 0;
    char* end = nullptr;
    const long long n = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE)
        throw std::runtime_error("ByteArray signed write: " + text + " does not fit in "
                                 + std::to_string(w) + " signed byte(s)");
    std::string out;
    packing("ByteArray signed write", [&] { protoio::encoding::putInt(out, n, w, big); return 0; });
    return makeBytes(ctx, out);
}

// ByteArray __packFloat: x width: 4|8 bigEndian: b. x is a Float or an Integer.
PRIM(prim_PackFloat) {
    ARGS(3, "__packFloat:width:bigEndian:");
    const PO* x = a[0];
    double d;
    if (x && x != PROTO_NONE && x->isFloat(ctx)) d = x->asDouble(ctx);
    else if (x && x != PROTO_NONE && x->isInteger(ctx)) {
        const std::string text = integerDigits(ctx, x, "ByteArray float write");
        d = std::strtod(text.c_str(), nullptr);
        if (std::isinf(d))
            throw std::runtime_error("ByteArray float write: the Integer " + text.substr(0, 20)
                                     + (text.size() > 20 ? "... " : " ") + "does not fit in a Float");
    }
    else throw std::runtime_error("ByteArray float write: a Float or an Integer was expected");
    const int w = widthArg(ctx, a[1], "ByteArray float write");
    if (w != 4 && w != 8) throw std::runtime_error("ByteArray float write: the width must be 4 or 8");
    const bool big = booleanArg(a[2], "ByteArray float write");
    std::string out;
    if (w == 8) {
        packing("ByteArray float write", [&] { protoio::encoding::putFloat64(out, d, big); return 0; });
    } else {
        const float f = static_cast<float>(d);
        if (std::isinf(f) && !std::isinf(d))
            throw std::runtime_error("ByteArray float write: " + std::to_string(d)
                                     + " does not fit in a 32-bit float");
        packing("ByteArray float write", [&] { protoio::encoding::putFloat32(out, f, big); return 0; });
    }
    return makeBytes(ctx, out);
}

#undef ARGS
#undef PRIM

} // namespace

void installBytesPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    const proto::ProtoObject* p = rt.bootstrap().byteArrayProto;
    struct Entry { const char* sel; PrimFn fn; };
    const Entry entries[] = {
        {"__bytesNew:", prim_BytesNew},           {"__bytesWithAll:", prim_BytesWithAll},
        {"__bytesSize", prim_BytesSize},          {"__bytesAt:", prim_BytesAt},
        {"__bytesCopyFrom:to:", prim_BytesCopyFromTo}, {"__bytesConcat:", prim_BytesConcat},
        {"__bytesEquals:", prim_BytesEquals},     {"__bytesHash", prim_BytesHash},
        {"__bytesHex", prim_BytesHex},            {"__fromHex:", prim_BytesFromHex},
        {"__utf8Encode:", prim_Utf8Encode},       {"__utf8Decode", prim_Utf8Decode},
        {"__bytesJoin:", prim_BytesJoin},
        {"__unpackUnsignedAt:width:bigEndian:", prim_UnpackUnsigned},
        {"__unpackSignedAt:width:bigEndian:", prim_UnpackSigned},
        {"__unpackFloatAt:width:bigEndian:", prim_UnpackFloat},
        {"__packUnsigned:width:bigEndian:", prim_PackUnsigned},
        {"__packSigned:width:bigEndian:", prim_PackSigned},
        {"__packFloat:width:bigEndian:", prim_PackFloat},
    };
    for (const Entry& e : entries) bindPrimitive(rt, p, e.sel, reg.registerPrim(e.fn));
}

} // namespace protoST
