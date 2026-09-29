#pragma once

// A primitive method is stored on a prototype as an integer attribute: the
// primitive's registry index tagged with bit 62. Only positive values carry
// the tag; a negative integer has bit 62 set in two's complement and is an
// ordinary value, never a primitive.
namespace protoST {

constexpr long long kPrimitiveMarkerBit = 1LL << 62;

constexpr long long encodePrimitiveMarker(int index) {
    return static_cast<long long>(index) | kPrimitiveMarkerBit;
}

constexpr bool isPrimitiveMarker(long long value) {
    return value > 0 && (value & kPrimitiveMarkerBit) != 0;
}

constexpr int primitiveMarkerIndex(long long marker) {
    return static_cast<int>(marker & (kPrimitiveMarkerBit - 1));
}

} // namespace protoST
