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
// deterministic. That each BinaryFile routes those three calls through its
// own one guard is what binfile_prims.cpp does (OpenFile::guard); protoIO's
// own tests check the guard's matrix itself.

#include <catch2/catch_all.hpp>

#include "primitives/BinFileGuard.h"
#include "runtime/NativeError.h"

#include <protoio/file.h>
#include <protoio/stream.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace {

using protoio::file::EndGuard;
using protoST::BinFileOp;

// A fresh 16-byte file, removed with its descriptor.
struct TempFile {
    std::string path;
    int fd;
    TempFile() {
        path = (std::filesystem::temp_directory_path()
                / ("protost-binfile-guard-" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
                   .string();
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
