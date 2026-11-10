#pragma once
// The two ways protoST code in C++ creates and reads a ByteArray.
//
// A ByteArray is protoCore's ProtoByteBuffer used as an immutable value
// (docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md, section
// 4.1). Its buffer is filled exactly once, when it is created, and never
// written again: protoST never calls ProtoByteBuffer::setAt. Every primitive
// that answers bytes builds them with makeBytes, and every primitive that
// takes bytes reads them with bytesView, so the guarantee holds in one place.

#include <string_view>

namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

// A new ByteArray holding a copy of `bytes`. Every octet 0-255 is kept,
// NUL included. The value is a fresh object owned by `ctx`, like any value
// a primitive allocates, and is returned as such.
const proto::ProtoObject* makeBytes(proto::ProtoContext* ctx, std::string_view bytes);

// The contents of the ByteArray `v`, read-only. Throws std::runtime_error,
// which reaches Smalltalk as an Error, when `v` is not a ByteArray. The view
// points into the value's own storage: it stays valid for as long as `v` is
// reachable (protoCore's collector does not move objects), so the caller
// copies out what it must keep beyond that.
std::string_view bytesView(proto::ProtoContext* ctx, const proto::ProtoObject* v);

} // namespace protoST
