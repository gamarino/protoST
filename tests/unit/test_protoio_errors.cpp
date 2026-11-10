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
#include "primitives/ProtoioErrors.h"
#include "protoCore.h"

#include <protoio/error.h>

#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Kind = protoio::Error::Kind;

// The kinds `Probe raise: n` raises, indexed by n, with the protoST class each
// must become and the message protoIO gives it.
struct Mapping {
    Kind kind;
    const char* className;
};
const Mapping kMappings[] = {
    {Kind::FileNotFound, "FileDoesNotExist"},
    {Kind::FileExists, "FileAlreadyExists"},
    {Kind::FileSystem, "FileSystemError"},
    {Kind::ConnectionRefused, "ConnectionRefused"},
    {Kind::ConnectionTimedOut, "ConnectionTimedOut"},
    {Kind::NameLookup, "NameLookupFailure"},
    {Kind::Network, "NetworkError"},
    {Kind::Process, "OSProcessError"},
    {Kind::LineTooLong, "LineTooLong"},
    {Kind::BodyTooLarge, "BodyTooLarge"},
    {Kind::InvalidArgument, "Error"},
    {Kind::Internal, "InternalError"},
};
constexpr const char* kProtoioMessage = "EVP_DigestFinal_ex failed (error:0308010C)";

// Probe raise: n -- raises the n-th mapping's kind through raiseProtoioError,
// with the operation prefix "ByteArray sha256".
const proto::ProtoObject* probeRaise(protoST::STRuntime&, proto::ProtoContext* ctx,
                                     const proto::ProtoObject*,
                                     const proto::ProtoObject* const* a, int argc) {
    const long long n = argc == 1 ? a[0]->asLong(ctx) : -1;
    if (n < 0 || n >= static_cast<long long>(std::size(kMappings)))
        throw std::logic_error("Probe raise: takes the index of a mapping");
    protoST::raiseProtoioError(protoio::Error(kMappings[n].kind, kProtoioMessage),
                               "ByteArray sha256");
}

struct Host {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;

    Host() {
        proto::ProtoContext* ctx = rt.rootCtx();
        auto* probe = rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/true);
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
    const int internal = static_cast<int>(std::size(kMappings)) - 1;
    REQUIRE(kMappings[internal].kind == Kind::Internal);
    const std::string n = std::to_string(internal);
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
    for (size_t i = 0; i < std::size(kMappings); ++i) {
        INFO("kind " << protoio::kindName(kMappings[i].kind));
        CHECK(h.run("[Probe raise: " + std::to_string(i) + "] on: Error do: [:e | "
                    "e class name , '|' , e messageText]")
              == std::string(kMappings[i].className) + "|ByteArray sha256: " + kProtoioMessage);
    }
}
