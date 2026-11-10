#pragma once
// The BinaryFile end guard's refusals in protoST's words.
//
// Every BinaryFile (binfile_prims.cpp, OpenFile) keeps one
// protoio::file::EndGuard beside its descriptor and routes append:,
// writeAt:bytes: and truncateTo: through protoIO's guarded overloads, which
// claim a use of the guard for the duration of the call and refuse one that
// conflicts with a use in progress with protoio::Error (kind Conflict),
// doing nothing (protoio/file.h, "the end guard"):
//
//   append:          refused while an append:, a truncateTo: or a
//                    writeAt:bytes: that extends the file is in progress;
//   writeAt:bytes:   refused while a truncateTo: is in progress, and, when it
//                    extends the file, while an append: is;
//   truncateTo:      refused while any of the three is in progress.
//
// refusingAs runs one guarded call and turns its Conflict into the Error the
// selector signals, with the texts below. tests/unit/test_binfile_guard.cpp
// holds the guard deterministically and checks every refusal.

#include "runtime/NativeError.h"
#include "primitives/ArgSupport.h"

#include <protoio/error.h>
#include <protoio/file.h>

#include <memory>
#include <string>

namespace protoST {

enum class BinFileOp { Append, WriteAt, Truncate };

// The selector, as the Error text names it.
constexpr const char* binFileSelector(BinFileOp op) {
    switch (op) {
        case BinFileOp::Append: return "BinaryFile>>append:";
        case BinFileOp::WriteAt: return "BinaryFile>>writeAt:bytes:";
        case BinFileOp::Truncate: return "BinaryFile>>truncateTo:";
    }
    return "BinaryFile";
}

// The text after the selector when the guard refuses `op`.
constexpr const char* binFileRefusal(BinFileOp op) {
    switch (op) {
        case BinFileOp::Append:
            return "another operation that moves the end of the file is in progress on this BinaryFile "
                   "(append: and truncateTo: need the end alone, and so does a writeAt:bytes: past the end)";
        case BinFileOp::WriteAt:
            return "a truncateTo: is in progress on this BinaryFile, or this write extends the file "
                   "while an append: is (truncateTo: needs the file alone, append: its end)";
        case BinFileOp::Truncate:
            return "another append:, writeAt:bytes: or truncateTo: is in progress on this BinaryFile "
                   "(truncateTo: needs the file alone)";
    }
    return "conflict";
}

// The full Error text of a refusal of `op`.
inline std::string binFileRefusalText(BinFileOp op) {
    return errorPrefix(binFileSelector(op)) + binFileRefusal(op);
}

// Runs `f`, one guarded protoIO call for `op`; a Conflict becomes NativeError
// (an Error in Smalltalk) with binFileRefusalText(op). Any other protoio::Error
// passes through to the bracket around the call.
template <typename F>
auto refusingAs(BinFileOp op, F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const protoio::Error& e) {
        if (e.kind != protoio::Error::Kind::Conflict) throw;
        throw NativeError(binFileRefusalText(op));
    }
}

// The end guard of the open BinaryFile `handle` (the handle instance variable
// of a BinaryFile), or nullptr when it is closed. The pointer keeps the file,
// and so its guard, alive. For tests: holding an EndGuard::Claim on it shows
// which use of the guard each primitive takes (test_binfile_guard.cpp).
std::shared_ptr<protoio::file::EndGuard> binFileGuardOf(long long handle);

} // namespace protoST
