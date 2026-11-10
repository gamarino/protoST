#pragma once
// Thrown by a primitive for an error of a specific Smalltalk class defined by
// the kernel (SubscriptOutOfBounds, KeyNotFound, NotFound, the I/O errors).
// The native-exception bridge (NativeExceptionBridge.h) signals an instance of
// the global of that name, so `on: KeyNotFound do:` and `on: Error do:` both
// catch it. It is internal to protoST: it never leaves the primitive boundary
// that translates it.
//
// A division by zero is not reported this way: a primitive raises protoCore's
// DivisionByZero error (ProtoContext::raiseError), which the runtime's error
// factory builds as a ZeroDivide (exception_prims.cpp), exactly as for a
// division by zero detected by protoCore itself.

#include <stdexcept>
#include <string>

namespace protoST {

class ClassedErrorSignal : public std::runtime_error {
public:
    ClassedErrorSignal(const char* className, const std::string& message)
        : std::runtime_error(message), className_(className), message_(message) {}
    const char* className() const { return className_; }
    // The full message. what() is a C string and ends at a NUL; a message
    // built from user text may hold one, so the bridge reads this instead.
    const std::string& message() const noexcept { return message_; }
private:
    const char* className_;
    std::string message_;
};

} // namespace protoST
