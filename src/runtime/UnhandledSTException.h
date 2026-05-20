#pragma once

// Track 1, slice 2 (EXC-d): UnhandledSTException — the C++ exception thrown by
// the exception protocol's `defaultAction` when a protoST exception reaches no
// handler.
//
// Why a dedicated type. EXC-d wraps every native/primitive call boundary in a
// translation `catch` that turns a `std::exception` into a catchable protoST
// `Error`. But `defaultAction` itself throws a C++ exception to abort an
// activation when an Error is unhandled — and that exception is ALREADY a
// protoST exception escaping to the top. If the translation `catch` caught it
// as a plain `std::exception`, it would re-wrap an already-protoST exception
// into a fresh `Error` and re-signal it: double translation, possibly a loop.
//
// The fix: `defaultAction` throws `UnhandledSTException` instead of a bare
// `std::runtime_error`, and the translation wrapper catches `UnhandledSTException`
// FIRST (before the generic `catch (const std::exception&)`) and re-throws it
// untouched.
//
// It DERIVES from `std::runtime_error` so the existing `drainOne` /
// `runTopLevel` `catch (const std::exception&)` clauses keep working unchanged
// — they still reject the message Future / print the message. Only the EXC-d
// translation wrapper needs to distinguish it, which it does by catching the
// more-derived type first.

#include <stdexcept>
#include <string>

namespace protoST {

class UnhandledSTException : public std::runtime_error {
public:
    explicit UnhandledSTException(const std::string& m)
        : std::runtime_error(m) {}
};

} // namespace protoST
