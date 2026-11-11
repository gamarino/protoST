#pragma once

// KNOWN_ISSUES K20: the exception an embedder receives from a runtime that
// owns its ProtoSpace.
//
// An unhandled protoST exception is a proto::ProtoException carrying the
// exception object, pinned in the object's space; protoCore's contract is
// that the exception must not outlive its space. A runtime made with
// `STRuntime()` owns its space and destroys it with itself, so the plain
// embedder pattern
//
//     try { protoST::STRuntime rt; rt.runTopLevel(m); }
//     catch (const std::exception& e) { ... }
//
// let the exception outlive the space, and its destructor crashed
// (ProtoRootSet::remove on freed memory). At the embedder boundary of such a
// runtime -- a public entry point called on protoST's own context with no
// protoST code running on the thread -- the ProtoException is therefore
// replaced by this self-contained exception: plain strings, no reference into
// the space. Everywhere else (a call nested in running protoST code, a call
// from another runtime's frame, a runtime that runs in a host's space) the
// ProtoException leaves unchanged, so handlers and other languages still
// select it by its value's prototype chain.

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace protoST {

class STUncaughtError : public std::runtime_error {
public:
    STUncaughtError(std::string description, std::string report, std::string trace,
                    std::vector<std::string> classNames)
        : std::runtime_error(description), report_(std::move(report)),
          trace_(std::move(trace)), classNames_(std::move(classNames)) {}

    // what(): the description ("AppError: too much"), as
    // UnhandledSTException::message() and describeProtoException give it.
    //
    // The report the `protost` binary prints: "error: <description>" and the
    // active methods when the error went unhandled, one per line.
    const std::string& report() const noexcept { return report_; }
    // The active methods alone ("  at Class>>selector (file:line)" lines);
    // empty when unknown.
    const std::string& trace() const noexcept { return trace_; }
    // The exception's class and its ancestors in resolution order -- the
    // superclasses and the `uses:` mixins alike, the classes `on:do:` selects
    // it by ({"ZeroDivide", "ArithmeticError", "Error", "Exception",
    // "Object"}). Empty for a value that is not an instance of a protoST
    // class.
    const std::vector<std::string>& classNames() const noexcept { return classNames_; }
    // Whether `on: <the class named name> do:` would have selected the
    // exception: an instance of that class or of a class that has it as an
    // ancestor (superclass or mixin), as `isKindOf:` answers.
    bool isKindOf(const std::string& name) const {
        for (const auto& n : classNames_) if (n == name) return true;
        return false;
    }

private:
    std::string report_;
    std::string trace_;
    std::vector<std::string> classNames_;
};

} // namespace protoST
