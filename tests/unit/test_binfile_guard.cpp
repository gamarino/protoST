// The BinaryFile end guard (src/primitives/BinFileGuard.h): every guarded
// call a BinaryFile primitive makes, under every use of the guard another
// call could be holding, with the Error text the primitive signals.
//
// A Smalltalk program cannot hold the guard for as long as it wants: a claim
// lives only inside one append:, writeAt:bytes: or truncateTo:, so which
// calls collide in the conformance programs depends on scheduling. Here the
// test itself holds a claim (protoio::file::EndGuard::Claim, the RAII claim
// the guarded calls take) on the guard it passes to the very calls the
// primitives make, wrapped in refusingAs as they are, so each verdict is
// deterministic. The last case runs the primitives themselves, from
// Smalltalk, while the test holds each use of the BinaryFile's own guard
// (binFileGuardOf): so it shows which use each primitive takes, and that it
// takes the guard of that BinaryFile. protoIO's own tests check the guard's
// matrix itself.

#include <catch2/catch_all.hpp>

#include "primitives/BinFileGuard.h"
#include "runtime/NativeError.h"
#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

#include <protoio/file.h>
#include <protoio/stream.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define PROTOST_TEST_PID _getpid()
#else
#include <unistd.h>
#define PROTOST_TEST_PID getpid()
#endif

namespace {

using protoio::file::EndGuard;
using protoST::BinFileOp;

// A path no other test process uses: CTest runs each case as its own process,
// several at once, and two of them can place an object at the same address.
std::string tempPath(const void* owner) {
    return (std::filesystem::temp_directory_path()
            / ("protost-binfile-guard-" + std::to_string(PROTOST_TEST_PID) + "-"
               + std::to_string(reinterpret_cast<std::uintptr_t>(owner))))
        .string();
}

// A fresh 16-byte file, removed with its descriptor.
struct TempFile {
    std::string path;
    int fd;
    TempFile() {
        path = tempPath(this);
        protoio::file::OpenOptions o;
        o.read = o.write = o.create = o.truncate = true;
        fd = protoio::file::open(path, o);
        protoio::file::writeAt(fd, 0, std::string(16, 'a'));
    }
    ~TempFile() {
        protoio::close(fd);
        std::filesystem::remove(path);
    }
};

// The call a primitive makes for `op`, as binfile_prims.cpp makes it: answers
// nullopt when it ran, or the Error text it was refused with. A writeAt is
// inside the 16-byte file, or extends it.
enum class Call { Append, WriteInside, WriteExtending, Truncate };
std::optional<std::string> run(Call call, int fd, EndGuard& guard) {
    try {
        switch (call) {
            case Call::Append:
                protoST::refusingAs(BinFileOp::Append,
                                    [&] { return protoio::file::appendData(fd, guard, "bb"); });
                break;
            case Call::WriteInside:
                protoST::refusingAs(BinFileOp::WriteAt,
                                    [&] { protoio::file::writeAt(fd, guard, 0, "cc"); });
                break;
            case Call::WriteExtending:
                protoST::refusingAs(BinFileOp::WriteAt,
                                    [&] { protoio::file::writeAt(fd, guard, 1000, "dd"); });
                break;
            case Call::Truncate:
                protoST::refusingAs(BinFileOp::Truncate,
                                    [&] { protoio::file::truncate(fd, guard, 8); });
                break;
        }
        return std::nullopt;
    } catch (const protoST::NativeError& e) {
        return e.message();
    }
}

std::string refusal(Call call) {
    switch (call) {
        case Call::Append: return protoST::binFileRefusalText(BinFileOp::Append);
        case Call::WriteInside:
        case Call::WriteExtending: return protoST::binFileRefusalText(BinFileOp::WriteAt);
        case Call::Truncate: return protoST::binFileRefusalText(BinFileOp::Truncate);
    }
    return {};
}

// Holds `held` and runs `call`: refused (with the selector's text, the file
// unchanged) or run, as `refused` says.
void expect(EndGuard::Use held, Call call, bool refused) {
    TempFile f;
    EndGuard guard;
    const std::string before = protoio::file::readAt(f.fd, 0, 64);
    std::optional<std::string> got;
    {
        EndGuard::Claim claim(guard, held, "test");
        got = run(call, f.fd, guard);
        if (refused) CHECK(protoio::file::readAt(f.fd, 0, 64) == before);
    }
    if (refused) {
        REQUIRE(got.has_value());
        CHECK(*got == refusal(call));
    } else {
        CHECK_FALSE(got.has_value());
    }
    CHECK(guard.state().idle());   // every claim released, refused or not
}

} // namespace

TEST_CASE("BinaryFile guard: while an append: is in progress", "[io][binfile]") {
    expect(EndGuard::Use::Append, Call::Append, true);
    expect(EndGuard::Use::Append, Call::WriteInside, false);
    expect(EndGuard::Use::Append, Call::WriteExtending, true);
    expect(EndGuard::Use::Append, Call::Truncate, true);
}

TEST_CASE("BinaryFile guard: while a truncateTo: is in progress", "[io][binfile]") {
    expect(EndGuard::Use::Truncate, Call::Append, true);
    expect(EndGuard::Use::Truncate, Call::WriteInside, true);
    expect(EndGuard::Use::Truncate, Call::WriteExtending, true);
    expect(EndGuard::Use::Truncate, Call::Truncate, true);
}

TEST_CASE("BinaryFile guard: while a writeAt:bytes: extends the file", "[io][binfile]") {
    // An extending write holds Extend together with Inside.
    TempFile f;
    EndGuard guard;
    EndGuard::Claim inside(guard, EndGuard::Use::Inside, "test");
    EndGuard::Claim extend(guard, EndGuard::Use::Extend, "test");
    CHECK(run(Call::Append, f.fd, guard) == refusal(Call::Append));
    CHECK_FALSE(run(Call::WriteInside, f.fd, guard).has_value());
    CHECK_FALSE(run(Call::WriteExtending, f.fd, guard).has_value());
    CHECK(run(Call::Truncate, f.fd, guard) == refusal(Call::Truncate));
}

TEST_CASE("BinaryFile guard: while a writeAt:bytes: inside the file is in progress",
          "[io][binfile]") {
    expect(EndGuard::Use::Inside, Call::Append, false);
    expect(EndGuard::Use::Inside, Call::WriteInside, false);
    expect(EndGuard::Use::Inside, Call::WriteExtending, false);
    // The new guarantee of protoIO 0.4.0: a write judged inside the file
    // cannot be turned into one past a truncated end.
    expect(EndGuard::Use::Inside, Call::Truncate, true);
}

TEST_CASE("BinaryFile guard: the refusal texts name the selector", "[io][binfile]") {
    CHECK(protoST::binFileRefusalText(BinFileOp::Append).rfind(
              "BinaryFile>>append: another operation that moves the end of the file is in progress", 0) == 0);
    CHECK(protoST::binFileRefusalText(BinFileOp::WriteAt).rfind(
              "BinaryFile>>writeAt:bytes: a truncateTo: is in progress", 0) == 0);
    CHECK(protoST::binFileRefusalText(BinFileOp::Truncate).rfind(
              "BinaryFile>>truncateTo: another append:, writeAt:bytes: or truncateTo: is in progress", 0) == 0);
}

namespace {

// A runtime with one BinaryFile, the global F, over a 16-byte file.
struct BinaryFileHost {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;
    std::string path = tempPath(this);
    std::shared_ptr<EndGuard> guard;

    BinaryFileHost() {
        std::filesystem::remove(path);
        run("Smalltalk at: #F put: (BinaryFile open: '" + path + "' options: #(#read #write #create)). "
            "F writeAt: 0 bytes: (ByteArray new: 16). 'ok'");
        const std::string handle = run("(F instVarNamed: 'handle') printString");
        guard = protoST::binFileGuardOf(std::stoll(handle));
        REQUIRE(guard != nullptr);
    }
    ~BinaryFileHost() {
        guard.reset();
        run("F close. 'ok'");
        std::filesystem::remove(path);
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

    // Runs `call` on F and answers 'ok' or the Error text it signalled.
    std::string attempt(const std::string& call) {
        return run("[" + call + ". 'ok'] on: Error do: [:e | e messageText]");
    }
};

} // namespace

TEST_CASE("BinaryFile guard: each primitive takes its own use of its file's guard",
          "[io][binfile]") {
    // While the test holds one use of F's guard, the primitives that conflict
    // with it are refused and the others run. A primitive that took the wrong
    // use (or another guard) would answer a different row: the four rows
    // tell append:, an inside and an extending writeAt:bytes:, and
    // truncateTo: apart.
    BinaryFileHost h;
    const std::string append = "F append: #[1 2]";
    const std::string inside = "F writeAt: 0 bytes: #[3 4]";
    const std::string extending = "F writeAt: 100000 bytes: #[5 6]";
    const std::string truncate = "F truncateTo: 16";
    const std::string ok = "ok";
    const auto refusal = [](BinFileOp op) { return protoST::binFileRefusalText(op); };

    struct Row {
        EndGuard::Use held;
        bool appendRefused, insideRefused, extendingRefused, truncateRefused;
    };
    const Row rows[] = {
        {EndGuard::Use::Append, true, false, true, true},
        {EndGuard::Use::Truncate, true, true, true, true},
        {EndGuard::Use::Extend, true, false, false, true},
        {EndGuard::Use::Inside, false, false, false, true},
    };
    for (const Row& row : rows) {
        INFO("held use " << static_cast<int>(row.held));
        {
            EndGuard::Claim claim(*h.guard, row.held, "test");
            CHECK(h.attempt(append) == (row.appendRefused ? refusal(BinFileOp::Append) : ok));
            CHECK(h.attempt(inside) == (row.insideRefused ? refusal(BinFileOp::WriteAt) : ok));
            CHECK(h.attempt(extending) == (row.extendingRefused ? refusal(BinFileOp::WriteAt) : ok));
            CHECK(h.attempt(truncate) == (row.truncateRefused ? refusal(BinFileOp::Truncate) : ok));
        }
        CHECK(h.guard->state().idle());
        // Back to 16 bytes, so the next row's inside write is inside again.
        CHECK(h.attempt(truncate) == ok);
    }
}
