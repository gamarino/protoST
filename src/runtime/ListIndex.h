#pragma once
// The one conversion from a C++ index or count to the `int` protoCore's
// ProtoList takes (getAt, setAt, insertAt, removeAt, getSlice).
//
// ProtoList::getSize answers an unsigned 64-bit count, but its accessors take
// an int. A plain static_cast<int> of an index past INT_MAX wraps (2^32 + 1
// becomes 1) and would read or replace the wrong element without a word.
// listIndex refuses such an index, and a negative one, with an Error instead.

#include "runtime/NativeError.h"

#include <climits>
#include <string>
#include <type_traits>
#include <utility>

namespace protoST {

template <typename I>
inline int listIndex(I i) {
    static_assert(std::is_integral_v<I> && !std::is_same_v<I, bool>,
                  "listIndex takes an integer index");
    if (std::cmp_less(i, 0) || std::cmp_greater(i, INT_MAX))
        throw NativeError("index " + std::to_string(i)
                          + " cannot address a collection: protoCore indexes at most 2^31 - 1 elements");
    return static_cast<int>(i);
}

} // namespace protoST
