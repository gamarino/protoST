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

#include "runtime/NativeError.h"

#include <string>

namespace protoST {

// A NativeError, so its full message (NUL included) is message(), read by
// nativeErrorText like that of any other native error.
class ClassedErrorSignal : public NativeError {
public:
    ClassedErrorSignal(const char* className, const std::string& message)
        : NativeError(message), className_(className) {}
    // An error that also carries an Integer in one instance variable of its
    // class (`ivar`, a name the kernel declares): ConnectionTimedOut's
    // bytesWritten.
    ClassedErrorSignal(const char* className, const std::string& message,
                       const char* ivar, long long value)
        : NativeError(message), className_(className), ivar_(ivar), ivarValue_(value) {}
    const char* className() const { return className_; }
    const char* ivar() const { return ivar_; }        // nullptr: none
    long long ivarValue() const { return ivarValue_; }
private:
    const char* className_;
    const char* ivar_ = nullptr;
    long long ivarValue_ = 0;
};

} // namespace protoST
