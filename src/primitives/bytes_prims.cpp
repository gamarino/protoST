#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "primitives/BytesSupport.h"
#include "runtime/Bootstrap.h"
#include "runtime/ClassedErrorSignal.h"
#include "protoCore.h"

#include <cstdint>
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
        ? a[0]->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"))
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
            throw std::runtime_error("ByteArray fromHex: '" + hex + "' is not hexadecimal");
        out.push_back(static_cast<char>((hi << 4) | lo));
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
    };
    for (const Entry& e : entries) bindPrimitive(rt, p, e.sel, reg.registerPrim(e.fn));
}

} // namespace protoST
