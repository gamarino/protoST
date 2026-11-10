#pragma once
// The one way protoST code in C++ creates a String from UTF-8 text held in a
// std::string or std::string_view.
//
// A protoST String may contain U+0000. ProtoContext::fromUTF8String reads its
// argument up to the first NUL, so building a result through it with
// `text.c_str()` silently drops everything after a NUL
// (docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md, section 2,
// anti-goals). Every primitive that builds a String from runtime data calls
// makeString instead; fromUTF8String stays only for fixed literals.

#include <string_view>

namespace proto { class ProtoContext; class ProtoObject; class ProtoString; }

namespace protoST {

// A new String holding `utf8`, NUL included. Malformed UTF-8 is tolerated as
// by fromUTF8String: a truncated trailing sequence degrades to its lead byte.
const proto::ProtoObject* makeString(proto::ProtoContext* ctx, std::string_view utf8);

// The Symbol whose name is `utf8`, NUL included. Use it where the name is
// runtime data (a selector computed by perform:); createSymbol(const char*)
// stays for identifiers and fixed names.
const proto::ProtoString* internName(proto::ProtoContext* ctx, std::string_view utf8);

} // namespace protoST
