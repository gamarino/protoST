#pragma once
// Thrown by the numeric primitives on division by zero. The native-exception
// bridge turns it into a ZeroDivide (a subclass of Error), so both
// `on: ZeroDivide do:` and `on: Error do:` catch it.

#include <stdexcept>
#include <string>

namespace protoST {

struct ZeroDivideSignal : std::runtime_error {
    ZeroDivideSignal() : std::runtime_error("ZeroDivide") {}
};

// Thrown by a primitive for an error of a specific Smalltalk class defined by
// the kernel (SubscriptOutOfBounds, KeyNotFound, NotFound). The bridge
// signals an instance of the global of that name, so `on: KeyNotFound do:`
// and `on: Error do:` both catch it.
class ClassedErrorSignal : public std::runtime_error {
public:
    ClassedErrorSignal(const char* className, const std::string& message)
        : std::runtime_error(message), className_(className) {}
    const char* className() const { return className_; }
private:
    const char* className_;
};

} // namespace protoST
