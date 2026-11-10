#pragma once
// Where a digest (a checksum, a hash, an HMAC, base64) runs, decided in one
// place: runDigest, used by every digest primitive in bytes_prims.cpp.
//
// Below kUnmanagedDigestBytes of input a digest runs inline: hashing a few KiB
// costs less than leaving and re-entering the collector's quorum. From there
// on it runs inside unmanaged() (IoBrackets.h), so a hash over a large input
// never holds up a stop-the-world collection while it runs. That is safe
// because the bracketed call touches no protoCore object: it reads only views
// taken before the bracket into ByteArrays the caller keeps reachable (the
// receiver and the arguments of the primitive), which are immutable
// (makeBytes fills a buffer once) and never moved (protoCore's collector does
// not move objects), or C++ copies. The result value is built after the
// bracket, with the context managed again.
//
// tests/unit/test_digest_runner.cpp observes the thread's state from inside
// the digest's function on both sides of the threshold.

#include "primitives/IoBrackets.h"
#include "protoCore.h"

#include <cstddef>
#include <utility>

namespace protoST {

// The input size, in bytes, from which a digest runs unmanaged.
inline constexpr std::size_t kUnmanagedDigestBytes = 64 * 1024;

// True when a digest over `inputBytes` bytes of input runs unmanaged.
constexpr bool digestRunsUnmanaged(std::size_t inputBytes) noexcept {
    return inputBytes >= kUnmanagedDigestBytes;
}

// Runs `f`, a digest over `inputBytes` bytes that touches no protoCore object,
// unmanaged or inline as digestRunsUnmanaged decides, and answers its result.
// A protoio::Error becomes the protoST error, its text prefixed with `who`.
template <class F>
auto runDigest(proto::ProtoContext* ctx, std::size_t inputBytes, const char* who, F&& f)
    -> decltype(f()) {
    if (digestRunsUnmanaged(inputBytes)) return unmanaged(ctx, std::forward<F>(f), who);
    return immediate(std::forward<F>(f), who);
}

} // namespace protoST
