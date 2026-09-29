#include "protoST/STRuntime.h"
#include "runtime/ZeroDivideSignal.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <cctype>
#include <algorithm>
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
    const proto::ProtoString* str = r->asString(ctx);
    if (!a[0]->isInteger(ctx)) throw std::runtime_error("String>>at: expects an Integer index");
    const long long idx = a[0]->asLong(ctx);
    const long long size = str ? static_cast<long long>(str->getSize(ctx)) : 0;
    if (idx < 1 || idx > size)
        throw ClassedErrorSignal("SubscriptOutOfBounds", "String>>at: index " + std::to_string(idx)
                                 + " out of bounds (size " + std::to_string(size) + ")");
    // protoCore answers the element as a Character (an embedded unicode char),
    // without decoding the whole string.
    return str->getAt(ctx, static_cast<int>(idx - 1));
}

// String>>asInteger / asNumber — parse the text as a number (Pharo), answering
// nil when it is not one. A long integer becomes a LargeInteger.
const proto::ProtoObject* parseNumber(proto::ProtoContext* ctx, const std::string& raw,
                                      bool integerOnly) {
    size_t b = 0, e = raw.size();
    while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
    const std::string t = raw.substr(b, e - b);
    if (t.empty()) return PROTO_NONE;
    size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    size_t digits = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) { ++i; ++digits; }
    if (digits == 0) return PROTO_NONE;
    if (i == t.size() || integerOnly) {
        const std::string intText = t.substr(0, i);
        try {
            return ctx->fromLong(std::stoll(intText));
        } catch (const std::out_of_range&) {
            return ctx->fromString(intText[0] == '+' ? intText.c_str() + 1 : intText.c_str(), 10);
        }
    }
    char* end = nullptr;
    const double d = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return PROTO_NONE;
    return ctx->fromDouble(d);
}

const proto::ProtoObject* prim_StrAsInteger(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    return parseNumber(ctx, toUtf8(r, ctx), /*integerOnly=*/true);
}

const proto::ProtoObject* prim_StrAsNumber(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    return parseNumber(ctx, toUtf8(r, ctx), /*integerOnly=*/false);
}

// Integer>>asCharacter — the Character with that code point.
const proto::ProtoObject* prim_NumAsCharacter(STRuntime&, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    if (!r->isInteger(ctx)) throw std::runtime_error("asCharacter expects an Integer");
    long long cp = r->asLong(ctx);
    if (cp < 0 || cp > 0x10FFFF)
        throw std::runtime_error("asCharacter: " + std::to_string(cp) + " is not a Unicode code point");
    return ctx->fromUnicodeChar(static_cast<unsigned int>(cp));
}

const proto::ProtoObject* prim_StrConcat(STRuntime& rt, proto::ProtoContext* ctx,
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
    // A Character argument is appended ('x' , Character cr). Any other
    // non-string argument is an error; it used to be dropped silently
    // ('x' , 3 answered 'x').
    if (!rhs && !a[0]->isInteger(ctx) && a[0]->getPrototype(ctx) == rt.bootstrap().characterProto) {
        const std::string out = toUtf8(r, ctx)
            + encodeCodepoint(static_cast<uint32_t>(a[0]->asLong(ctx)));
        return ctx->fromUTF8String(out.c_str());
    }
    if (!rhs)
        throw std::runtime_error("Cannot append a non-string to a String "
                                 "(use printString or displayString to convert it)");
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


// --- Character ------------------------------------------------------------
// A Character is protoCore's embedded unicode-char value; asLong answers its
// code point.
const proto::ProtoObject* prim_CharValue(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    return ctx->fromLong(r->asLong(ctx));
}

const proto::ProtoObject* prim_CharAsString(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    return ctx->fromUTF8String(encodeCodepoint(static_cast<uint32_t>(r->asLong(ctx))).c_str());
}

// --- String operations over UTF-8 code points ---------------------------------
std::string encodeAll(const std::vector<uint32_t>& cps, size_t from, size_t to) {
    std::string out;
    for (size_t i = from; i < to && i < cps.size(); ++i) out += encodeCodepoint(cps[i]);
    return out;
}

const proto::ProtoObject* prim_StrCopyFromTo(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a, int argc) {
    if (argc != 2) throw std::runtime_error("copyFrom:to: expects 2 args");
    const std::vector<uint32_t> cps = decodeCodepoints(toUtf8(r, ctx));
    const long long from = a[0]->asLong(ctx), to = a[1]->asLong(ctx);
    if (to < from) return ctx->fromUTF8String("");
    if (from < 1 || to > static_cast<long long>(cps.size()))
        throw ClassedErrorSignal("SubscriptOutOfBounds", "copyFrom:to: range " + std::to_string(from) + " to "
                                 + std::to_string(to) + " out of bounds (size "
                                 + std::to_string(cps.size()) + ")");
    return ctx->fromUTF8String(encodeAll(cps, static_cast<size_t>(from - 1),
                                         static_cast<size_t>(to)).c_str());
}

uint32_t caseMap(uint32_t cp, bool upper) {
    if (cp < 0x80) return upper ? static_cast<uint32_t>(std::toupper(static_cast<int>(cp)))
                                : static_cast<uint32_t>(std::tolower(static_cast<int>(cp)));
    // Latin-1 letters (a-grave .. thorn), except the multiplication/division signs.
    if (upper && cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
    if (!upper && cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
    return cp;
}

const proto::ProtoObject* mapCase(proto::ProtoContext* ctx, const proto::ProtoObject* r, bool upper) {
    std::vector<uint32_t> cps = decodeCodepoints(toUtf8(r, ctx));
    for (auto& cp : cps) cp = caseMap(cp, upper);
    return ctx->fromUTF8String(encodeAll(cps, 0, cps.size()).c_str());
}

const proto::ProtoObject* prim_StrAsUppercase(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const*, int) {
    return mapCase(ctx, r, true);
}

const proto::ProtoObject* prim_StrAsLowercase(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const*, int) {
    return mapCase(ctx, r, false);
}

const proto::ProtoObject* prim_CharAsUppercase(STRuntime&, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    return ctx->fromUnicodeChar(caseMap(static_cast<uint32_t>(r->asLong(ctx)), true));
}

const proto::ProtoObject* prim_CharAsLowercase(STRuntime&, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    return ctx->fromUnicodeChar(caseMap(static_cast<uint32_t>(r->asLong(ctx)), false));
}

const proto::ProtoObject* prim_StrReversed(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    std::vector<uint32_t> cps = decodeCodepoints(toUtf8(r, ctx));
    std::reverse(cps.begin(), cps.end());
    return ctx->fromUTF8String(encodeAll(cps, 0, cps.size()).c_str());
}

std::string stringArg(proto::ProtoContext* ctx, const proto::ProtoObject* o, const char* who) {
    const proto::ProtoString* s = o ? o->asString(ctx) : nullptr;
    if (!s) throw std::runtime_error(std::string(who) + " expects a String argument");
    return s->toStdString(ctx);
}

const proto::ProtoObject* prim_StrIncludesSubstring(STRuntime&, proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* r,
                                                    const proto::ProtoObject* const* a, int) {
    return toUtf8(r, ctx).find(stringArg(ctx, a[0], "includesSubstring:")) != std::string::npos
        ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_StrCopyReplaceAll(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a, int argc) {
    if (argc != 2) throw std::runtime_error("copyReplaceAll:with: expects 2 args");
    std::string s = toUtf8(r, ctx);
    const std::string from = stringArg(ctx, a[0], "copyReplaceAll:with:");
    const std::string to = stringArg(ctx, a[1], "copyReplaceAll:with:");
    if (from.empty()) return ctx->fromUTF8String(s.c_str());
    std::string out;
    size_t pos = 0, hit;
    while ((hit = s.find(from, pos)) != std::string::npos) {
        out.append(s, pos, hit - pos);
        out += to;
        pos = hit + from.size();
    }
    out.append(s, pos, std::string::npos);
    return ctx->fromUTF8String(out.c_str());
}

const proto::ProtoObject* prim_StrTrimBoth(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    const std::string s = toUtf8(r, ctx);
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return ctx->fromUTF8String(s.substr(b, e - b).c_str());
}

// substrings / substrings: separators — an Array of the pieces between
// separator characters (whitespace by default), empty pieces dropped.
const proto::ProtoObject* splitToArray(STRuntime& rt, proto::ProtoContext* ctx,
                                       const std::string& s, const std::vector<uint32_t>& seps) {
    const std::vector<uint32_t> cps = decodeCodepoints(s);
    auto isSep = [&](uint32_t c) {
        if (seps.empty()) return c < 0x80 && std::isspace(static_cast<int>(c));
        return std::find(seps.begin(), seps.end(), c) != seps.end();
    };
    const proto::ProtoList* parts = ctx->newList();
    size_t start = 0;
    for (size_t i = 0; i <= cps.size(); ++i) {
        if (i == cps.size() || isSep(cps[i])) {
            if (i > start)
                parts = parts->appendLast(ctx, ctx->fromUTF8String(encodeAll(cps, start, i).c_str()));
            start = i + 1;
        }
    }
    const proto::ProtoObject* arr = rt.bootstrap().arrayProto->newChild(ctx, /*isMutable=*/true);
    arr->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"), parts->asObject(ctx));
    return arr;
}

const proto::ProtoObject* prim_StrSubstrings(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    return splitToArray(rt, ctx, toUtf8(r, ctx), {});
}

const proto::ProtoObject* prim_StrSubstringsSep(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* a, int) {
    return splitToArray(rt, ctx, toUtf8(r, ctx),
                        decodeCodepoints(stringArg(ctx, a[0], "substrings:")));
}

// Ordering by code points (Pharo compares case-sensitively too).
int compareStrings(proto::ProtoContext* ctx, const proto::ProtoObject* r,
                   const proto::ProtoObject* o) {
    const int c = toUtf8(r, ctx).compare(stringArg(ctx, o, "String comparison"));
    return (c > 0) - (c < 0);
}
const proto::ProtoObject* prim_StrLt(STRuntime&, proto::ProtoContext* ctx, const proto::ProtoObject* r,
                                     const proto::ProtoObject* const* a, int) {
    return compareStrings(ctx, r, a[0]) < 0 ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_StrLe(STRuntime&, proto::ProtoContext* ctx, const proto::ProtoObject* r,
                                     const proto::ProtoObject* const* a, int) {
    return compareStrings(ctx, r, a[0]) <= 0 ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_StrGt(STRuntime&, proto::ProtoContext* ctx, const proto::ProtoObject* r,
                                     const proto::ProtoObject* const* a, int) {
    return compareStrings(ctx, r, a[0]) > 0 ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_StrGe(STRuntime&, proto::ProtoContext* ctx, const proto::ProtoObject* r,
                                     const proto::ProtoObject* const* a, int) {
    return compareStrings(ctx, r, a[0]) >= 0 ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_StrAsSymbol(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    return reinterpret_cast<const proto::ProtoObject*>(
        proto::ProtoString::createSymbol(ctx, toUtf8(r, ctx).c_str()));
}

const proto::ProtoObject* prim_SymAsString(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    return ctx->fromUTF8String(toUtf8(r, ctx).c_str());
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
    bindPrimitive(rt, b.stringProto, "asNumber",   reg.registerPrim(prim_StrAsNumber));
    bindPrimitive(rt, b.stringProto, "copyFrom:to:", reg.registerPrim(prim_StrCopyFromTo));
    bindPrimitive(rt, b.stringProto, "asUppercase", reg.registerPrim(prim_StrAsUppercase));
    bindPrimitive(rt, b.stringProto, "asLowercase", reg.registerPrim(prim_StrAsLowercase));
    bindPrimitive(rt, b.stringProto, "reversed",   reg.registerPrim(prim_StrReversed));
    bindPrimitive(rt, b.stringProto, "includesSubstring:", reg.registerPrim(prim_StrIncludesSubstring));
    bindPrimitive(rt, b.stringProto, "copyReplaceAll:with:", reg.registerPrim(prim_StrCopyReplaceAll));
    bindPrimitive(rt, b.stringProto, "trimBoth",   reg.registerPrim(prim_StrTrimBoth));
    bindPrimitive(rt, b.stringProto, "substrings", reg.registerPrim(prim_StrSubstrings));
    bindPrimitive(rt, b.stringProto, "substrings:", reg.registerPrim(prim_StrSubstringsSep));
    bindPrimitive(rt, b.stringProto, "<",          reg.registerPrim(prim_StrLt));
    bindPrimitive(rt, b.stringProto, "<=",         reg.registerPrim(prim_StrLe));
    bindPrimitive(rt, b.stringProto, ">",          reg.registerPrim(prim_StrGt));
    bindPrimitive(rt, b.stringProto, ">=",         reg.registerPrim(prim_StrGe));
    bindPrimitive(rt, b.stringProto, "asSymbol",   reg.registerPrim(prim_StrAsSymbol));
    bindPrimitive(rt, b.symbolProto, "asString",   reg.registerPrim(prim_SymAsString));
    bindPrimitive(rt, b.characterProto, "value",    reg.registerPrim(prim_CharValue));
    bindPrimitive(rt, b.characterProto, "asString", reg.registerPrim(prim_CharAsString));
    bindPrimitive(rt, b.characterProto, "asUppercase", reg.registerPrim(prim_CharAsUppercase));
    bindPrimitive(rt, b.characterProto, "asLowercase", reg.registerPrim(prim_CharAsLowercase));
    bindPrimitive(rt, b.objectProto, "printNl", reg.registerPrim(prim_PrintNl)); // fallback
}

} // namespace protoST
