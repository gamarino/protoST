#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

namespace {

// protoCore exposes UTF-8 conversion via ProtoObject::asString(ctx)->toStdString(ctx).
// There is no direct ProtoObject::asUTF8String; the plan text was off, but the
// reachable API is equivalent and idiomatic across protoJS/protoPython.
static inline std::string toUtf8(const proto::ProtoObject* o, proto::ProtoContext* ctx) {
    return o->asString(ctx)->toStdString(ctx);
}

// --- UTF-8 codepoint helpers -------------------------------------------------
//
// protoST character literals are 1-character Strings (the F2 simplification in
// STRuntime::materialize), and the String protocol historically exposed no
// character access at all. The JSON module (T4-d) — and any text-processing
// stdlib module — needs to scan a String character by character. These helpers
// add the minimal, idiomatic Smalltalk String accessors (`at:`, `asArray`,
// `asInteger`) over proper UTF-8 codepoint boundaries.

// Decode the UTF-8 byte string into a vector of codepoints. Malformed bytes are
// passed through as their raw byte value, so the function never throws.
static std::vector<uint32_t> decodeCodepoints(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int extra;
        if (c < 0x80)        { cp = c;          extra = 0; }
        else if ((c >> 5) == 0x6)  { cp = c & 0x1F;  extra = 1; }
        else if ((c >> 4) == 0xE)  { cp = c & 0x0F;  extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07;  extra = 3; }
        else                 { out.push_back(c); ++i; continue; }
        if (i + extra >= n) { out.push_back(c); ++i; continue; }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 0x2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back(c); ++i; continue; }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

// Encode a single codepoint to a UTF-8 byte string.
static std::string encodeCodepoint(uint32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

// String>>at: — the n-th character (1-based) as a 1-character String.
// An out-of-bounds index throws std::runtime_error, translated to a catchable
// protoST Error like the collection `at:` primitives.
const proto::ProtoObject* prim_StrAt(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("String>>at: expects 1 arg (index)");
    std::vector<uint32_t> cps = decodeCodepoints(toUtf8(r, ctx));
    long long idx = a[0]->asLong(ctx);
    if (idx < 1 || idx > static_cast<long long>(cps.size())) {
        throw std::runtime_error("String>>at: index out of bounds");
    }
    return ctx->fromUTF8String(encodeCodepoint(cps[idx - 1]).c_str());
}

// String>>asInteger — the Unicode code point of the first character.
// Answers nil for the empty string.
const proto::ProtoObject* prim_StrAsInteger(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    std::vector<uint32_t> cps = decodeCodepoints(toUtf8(r, ctx));
    if (cps.empty()) return PROTO_NONE;
    return ctx->fromLong(static_cast<long long>(cps[0]));
}

// Number>>asCharacter — the code point as a 1-character String. The inverse of
// String>>asInteger; bound on the number prototype.
const proto::ProtoObject* prim_NumAsCharacter(STRuntime&, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    long long cp = r->asLong(ctx);
    if (cp < 0 || cp > 0x10FFFF) {
        throw std::runtime_error("Number>>asCharacter argument out of Unicode range");
    }
    return ctx->fromUTF8String(encodeCodepoint(static_cast<uint32_t>(cp)).c_str());
}

const proto::ProtoObject* prim_StrConcat(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int) {
    // Rope-aware fast path: protoCore strings carry a rope spine, and
    // `ProtoString::appendLast` builds a new internal node in O(log N)
    // rather than materialising both operands to UTF-8 and rebuilding a
    // fresh leaf (the legacy path below makes `s := s , 'x'` over N
    // iterations cost O(N²)).
    //
    // The earlier attempt at this (commit 21149f0) had to revert because
    // protoCore's `StringInternalNode::subtreeHash` is structural: a rope
    // `'memb' , 'ers'` and a leaf `'members'` are byte-equal yet hash
    // differently, which broke `Dictionary>>at:` against rope-built keys.
    // That has now been addressed at the consumer side — see
    // `collection_prims.cpp::dictKeyHash`, which canonicalises String keys
    // through `ProtoString::createSymbol` before hashing. Two strings with
    // identical content (regardless of rope vs leaf shape) collapse to the
    // same canonical symbol pointer and therefore to the same Dictionary
    // slot. This primitive can finally take the cheap path.
    const proto::ProtoString* lhs = r->asString(ctx);
    const proto::ProtoString* rhs = a[0]->asString(ctx);
    if (lhs && rhs) {
        const proto::ProtoString* concatenated = lhs->appendLast(ctx, rhs);
        if (concatenated) return concatenated->asObject(ctx);
    }
    // Fall back to UTF-8 materialisation for receivers whose `asString`
    // does not surface a ProtoString (e.g. a subclass overriding it to
    // return nil, or a non-string mistakenly fed to `,`).
    std::string out = toUtf8(r, ctx) + toUtf8(a[0], ctx);
    return ctx->fromUTF8String(out.c_str());
}

const proto::ProtoObject* prim_StrSize(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    // Character count (codepoints), consistent with `at:` which is codepoint-
    // indexed — for pure ASCII this equals the byte count.
    return ctx->fromLong(
        static_cast<long long>(decodeCodepoints(toUtf8(r, ctx)).size()));
}

const proto::ProtoObject* prim_StrEq(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    return (toUtf8(r, ctx) == toUtf8(a[0], ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

// D18: `~=` on String — value-inequality. Object's default `~=` is
// identity-negation, which would wrongly report two distinct equal-valued
// String objects as unequal; String overrides it to compare contents.
const proto::ProtoObject* prim_StrNe(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    return (toUtf8(r, ctx) != toUtf8(a[0], ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_PrintNl(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    // `formatValue` renders strings, the whole numeric tower (SmallInteger /
    // LargeInteger / Float) and the default object form. protoCore's
    // `asString` answers nil for a number, so a bare `asString` here would
    // fault on `2 printNl` / `25 factorial printNl`.
    std::string s = formatValue(rt, ctx, r);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    return r;
}

} // anon

void installStringPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.stringProto, ",",       reg.registerPrim(prim_StrConcat));
    bindPrimitive(rt, b.stringProto, "size",    reg.registerPrim(prim_StrSize));
    bindPrimitive(rt, b.stringProto, "=",       reg.registerPrim(prim_StrEq));
    bindPrimitive(rt, b.stringProto, "~=",      reg.registerPrim(prim_StrNe));
    bindPrimitive(rt, b.stringProto, "printNl", reg.registerPrim(prim_PrintNl));
    // Character access — the n-th character as a 1-char String, and the
    // codepoint of the first character. The String protocol previously had no
    // character access at all; these are the minimal idiomatic accessors a
    // text-processing stdlib module (JSON, T4-d) needs.
    bindPrimitive(rt, b.stringProto, "at:",        reg.registerPrim(prim_StrAt));
    bindPrimitive(rt, b.stringProto, "asInteger",  reg.registerPrim(prim_StrAsInteger));
    // The inverse, bound on the shared number prototype: codepoint -> 1-char
    // String. Lets a module render an escape (`\n`, `\uXXXX`) from a number.
    bindPrimitive(rt, b.numberProto, "asCharacter", reg.registerPrim(prim_NumAsCharacter));
    bindPrimitive(rt, b.objectProto, "printNl", reg.registerPrim(prim_PrintNl)); // fallback
}

} // namespace protoST
