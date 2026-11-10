#pragma once
// The one conversion from a C++ index or count to the `int` protoCore's
// ProtoList takes (getAt, setAt, insertAt, removeAt, getSlice).
//
// ProtoList::getSize answers an unsigned 64-bit count, but its accessors take
// an int. A plain static_cast<int> of an index past INT_MAX wraps (2^32 + 1
// becomes 1) and would read or replace the wrong element without a word.
// listIndex refuses such an index, and a negative one, with an Error instead.
//
// listIndex is inlined into hot paths (the engine's super sends, the handler
// stack): it is one comparison and a branch. Building and throwing the error
// is out of line and marked cold, so the dispatch loop carries no string
// temporaries and no landing pad for it.

#include "runtime/NativeError.h"

#include <climits>
#include <string>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && !defined(__clang__)
#define PROTOST_COLD_NOINLINE __declspec(noinline)
#else
#define PROTOST_COLD_NOINLINE __attribute__((cold, noinline))
#endif

namespace protoST {

// Throw the Error for an index no ProtoList can address.
[[noreturn]] PROTOST_COLD_NOINLINE inline void throwListIndexOutOfRange(long long index) {
    throw NativeError("index " + std::to_string(index)
                      + " cannot address a collection: protoCore indexes at most 2^31 - 1 elements");
}
[[noreturn]] PROTOST_COLD_NOINLINE inline void throwListIndexOutOfRange(unsigned long long index) {
    throw NativeError("index " + std::to_string(index)
                      + " cannot address a collection: protoCore indexes at most 2^31 - 1 elements");
}

template <typename I>
inline int listIndex(I i) {
    static_assert(std::is_integral_v<I> && !std::is_same_v<I, bool>,
                  "listIndex takes an integer index");
    if (std::cmp_less(i, 0) || std::cmp_greater(i, INT_MAX)) [[unlikely]]
    {
        if constexpr (std::is_signed_v<I>) throwListIndexOutOfRange(static_cast<long long>(i));
        else throwListIndexOutOfRange(static_cast<unsigned long long>(i));
    }
    return static_cast<int>(i);
}

} // namespace protoST
