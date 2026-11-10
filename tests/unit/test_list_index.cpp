// listIndex (src/runtime/ListIndex.h): the one conversion from a C++ index to
// the int protoCore's ProtoList accessors take. A collection with more than
// 2^31 - 1 elements cannot be built in a unit test, so the conversion is
// tested directly: an index past INT_MAX, or a negative one, is refused with a
// NativeError (an Error in Smalltalk) instead of wrapping to another element.

#include <catch2/catch_all.hpp>

#include "runtime/ListIndex.h"
#include "runtime/NativeError.h"

#include <climits>
#include <cstddef>
#include <cstdint>

TEST_CASE("listIndex: an index in the int range is answered unchanged", "[collections]") {
    CHECK(protoST::listIndex(std::size_t{0}) == 0);
    CHECK(protoST::listIndex(std::size_t{41}) == 41);
    CHECK(protoST::listIndex(static_cast<std::size_t>(INT_MAX)) == INT_MAX);
    CHECK(protoST::listIndex(7LL) == 7);
    CHECK(protoST::listIndex(INT_MAX) == INT_MAX);
}

TEST_CASE("listIndex: an index past INT_MAX or below zero is refused, never wrapped",
          "[collections]") {
    // static_cast<int> would answer 1 for 2^32 + 1 and INT_MIN for 2^31.
    CHECK_THROWS_AS(protoST::listIndex((std::uint64_t{1} << 32) + 1), protoST::NativeError);
    CHECK_THROWS_AS(protoST::listIndex(static_cast<std::size_t>(INT_MAX) + 1), protoST::NativeError);
    CHECK_THROWS_AS(protoST::listIndex(~std::uint64_t{0}), protoST::NativeError);
    CHECK_THROWS_AS(protoST::listIndex(-1LL), protoST::NativeError);
    CHECK_THROWS_AS(protoST::listIndex(LLONG_MIN), protoST::NativeError);
    CHECK_THROWS_WITH(protoST::listIndex((std::uint64_t{1} << 32) + 1),
                      Catch::Matchers::StartsWith("index 4294967297 cannot address a collection"));
}
