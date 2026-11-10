#pragma once
// The one mapping from a protoio::Error to the protoST error it becomes, shared
// by every primitive file that calls protoIO (io_prims.cpp, bytes_prims.cpp).

#include "runtime/ClassedErrorSignal.h"

#include <protoio/error.h>

#include <stdexcept>
#include <string>

namespace protoST {

// Throws the protoST error for `e`, its message prefixed with `who` (the
// operation, "" for none). InvalidArgument (a value the library refuses) is the
// generic Error the primitives raise for a bad argument. Internal (a failure
// inside a library protoIO relies on, such as OpenSSL, caused neither by the
// program nor by its environment) is an InternalError. The other kinds keep
// their own classes.
[[noreturn]] inline void raiseProtoioError(const protoio::Error& e, const std::string& who = {}) {
    using Kind = protoio::Error::Kind;
    const std::string text = who.empty() ? std::string(e.what()) : who + ": " + e.what();
    switch (e.kind) {
        case Kind::FileNotFound: throw ClassedErrorSignal("FileDoesNotExist", text);
        case Kind::FileExists: throw ClassedErrorSignal("FileAlreadyExists", text);
        case Kind::FileSystem: throw ClassedErrorSignal("FileSystemError", text);
        case Kind::ConnectionRefused: throw ClassedErrorSignal("ConnectionRefused", text);
        case Kind::ConnectionTimedOut: throw ClassedErrorSignal("ConnectionTimedOut", text);
        case Kind::NameLookup: throw ClassedErrorSignal("NameLookupFailure", text);
        case Kind::Network: throw ClassedErrorSignal("NetworkError", text);
        case Kind::Process: throw ClassedErrorSignal("OSProcessError", text);
        case Kind::LineTooLong: throw ClassedErrorSignal("LineTooLong", text);
        case Kind::BodyTooLarge: throw ClassedErrorSignal("BodyTooLarge", text);
        case Kind::Internal: throw ClassedErrorSignal("InternalError", text);
        case Kind::InvalidArgument: break;
    }
    throw std::runtime_error(text);
}

} // namespace protoST
