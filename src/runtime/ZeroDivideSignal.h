#pragma once
// Thrown by the numeric primitives on division by zero. The native-exception
// bridge turns it into a ZeroDivide (a subclass of Error), so both
// `on: ZeroDivide do:` and `on: Error do:` catch it.

#include <stdexcept>

namespace protoST {

struct ZeroDivideSignal : std::runtime_error {
    ZeroDivideSignal() : std::runtime_error("ZeroDivide") {}
};

} // namespace protoST
