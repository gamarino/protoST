// raiseProtoioError (src/primitives/ProtoioErrors.h): the one mapping from a
// protoio::Error to the protoST error a primitive signals.
//
// protoIO raises Kind::Internal only for failures inside a library it relies
// on (OpenSSL), which no program can provoke, and the shipped runtime has no
// primitive that raises an arbitrary protoio::Error. So this file registers
// one in its own runtime only: `Probe raise: n`, a primitive that calls
// raiseProtoioError with a synthesized protoio::Error of the n-th kind. The
// error then travels the path of every protoIO failure: the primitive
// boundary (translateNativeException) signals it, and a Smalltalk handler
// reads its class and messageText.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/TransientPin.h"
#include "primitives/ProtoioErrors.h"
#include "protoCore.h"

#include <protoio/error.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Kind = protoio::Error::Kind;

// The protoST class each kind must become. The switch names every kind and
// has no default, and -Wswitch is an error in this file: a kind protoIO adds
// later fails this test's build until it is listed here (and mapped in
// raiseProtoioError, whose switch is exhaustive the same way).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#endif
const char* expectedClass(Kind kind) {
    switch (kind) {
        case Kind::FileNotFound: return "FileDoesNotExist";
        case Kind::FileExists: return "FileAlreadyExists";
        case Kind::FileSystem: return "FileSystemError";
        case Kind::ConnectionRefused: return "ConnectionRefused";
        case Kind::ConnectionTimedOut: return "ConnectionTimedOut";
        case Kind::NameLookup: return "NameLookupFailure";
        case Kind::Network: return "NetworkError";
        case Kind::Process: return "OSProcessError";
        case Kind::LineTooLong: return "LineTooLong";
        case Kind::BodyTooLarge: return "BodyTooLarge";
        case Kind::InvalidArgument: return "Error";
        case Kind::Internal: return "InternalError";
        case Kind::Conflict: return "Error";
    }
    return nullptr;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

// The last enumerator of protoio::Error::Kind; the enumerators run from 0
// to it without gaps (protoio/error.h appends new kinds). A kind added after
// it is caught by the switch above.
constexpr Kind kLastKind = Kind::Conflict;
constexpr int kKindCount = static_cast<int>(kLastKind) + 1;
static_assert(static_cast<int>(Kind::FileNotFound) == 0, "the kinds start at 0");

constexpr const char* kProtoioMessage = "EVP_DigestFinal_ex failed (error:0308010C)";

// Probe raise: n -- raises the kind numbered n through raiseProtoioError,
// with the operation prefix "ByteArray sha256".
const proto::ProtoObject* probeRaise(protoST::STRuntime&, proto::ProtoContext* ctx,
                                     const proto::ProtoObject*,
                                     const proto::ProtoObject* const* a, int argc) {
    const long long n = argc == 1 ? a[0]->asLong(ctx) : -1;
    if (n < 0 || n >= kKindCount)
        throw std::logic_error("Probe raise: takes the number of a kind");
    protoST::raiseProtoioError(protoio::Error(static_cast<Kind>(n), kProtoioMessage),
                               "ByteArray sha256");
}

struct Host {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;

    Host() {
        proto::ProtoContext* ctx = rt.rootCtx();
        auto* probe = rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/true);
        // Held across the allocations below (bindPrimitive, createSymbol)
        // until the global keeps it.
        protoST::TransientPin pinProbe(ctx, probe);
        protoST::bindPrimitive(rt, probe, "raise:", rt.registry().registerPrim(probeRaise));
        rt.globals()->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Probe"), probe);
    }

    std::string run(const std::string& src) {
        protoST::Parser P(src);
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        REQUIRE(!C.hasErrors());
        kept.push_back(std::move(bc));
        const proto::ProtoObject* r = rt.runTopLevel(*kept.back());
        REQUIRE(r != nullptr);
        const proto::ProtoString* s = r->asString(rt.rootCtx());
        REQUIRE(s != nullptr);
        return s->toStdString(rt.rootCtx());
    }
};

} // namespace

TEST_CASE("protoIO errors: Kind::Internal is signalled as InternalError with its text",
          "[io][exceptions]") {
    Host h;
    const std::string n = std::to_string(static_cast<int>(Kind::Internal));
    // Selected by its own class, whose name is InternalError, with protoIO's
    // message after the operation's prefix.
    CHECK(h.run("[Probe raise: " + n + "] on: InternalError do: [:e | "
                "e class name , '|' , e messageText]")
          == std::string("InternalError|ByteArray sha256: ") + kProtoioMessage);
    // An Error: a generic handler takes it too.
    CHECK(h.run("[Probe raise: " + n + "] on: Error do: [:e | e class name]")
          == "InternalError");
}

TEST_CASE("protoIO errors: every kind maps to its protoST class", "[io][exceptions]") {
    Host h;
    for (int i = 0; i < kKindCount; ++i) {
        const Kind kind = static_cast<Kind>(i);
        INFO("kind " << protoio::kindName(kind));
        CHECK(h.run("[Probe raise: " + std::to_string(i) + "] on: Error do: [:e | "
                    "e class name , '|' , e messageText]")
              == std::string(expectedClass(kind)) + "|ByteArray sha256: " + kProtoioMessage);
    }
}
