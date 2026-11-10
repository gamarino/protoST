// runDigest (src/primitives/DigestRunner.h): a digest over 64 KiB of input or
// more runs in an unmanaged region, so it never holds up a stop-the-world
// collection; a smaller one runs inline.
//
// protoCore has no query for the calling thread's own unmanaged depth. What
// it publishes is ProtoSpace::parkedThreads, the number of the space's threads
// that are out of the collector's quorum (an unmanaged region's outermost
// entry adds one; tests/unit/test_worker_thread.cpp reads it the same way).
// These cases run in a bare ProtoSpace whose only thread is the test's own,
// so that count changes only when this thread enters or leaves an unmanaged
// region: the digest's function reads it from inside, and the reading is
// exact whatever the scheduling (no worker pool, no other mutator).

#include <catch2/catch_all.hpp>

#include "primitives/DigestRunner.h"
#include "runtime/ClassedErrorSignal.h"
#include "protoCore.h"

#include <protoio/digest.h>
#include <protoio/error.h>

#include <string>

TEST_CASE("runDigest: the threshold is 64 KiB of input", "[bytes][gc]") {
    STATIC_REQUIRE(protoST::kUnmanagedDigestBytes == 64 * 1024);
    STATIC_REQUIRE(!protoST::digestRunsUnmanaged(0));
    STATIC_REQUIRE(!protoST::digestRunsUnmanaged(64 * 1024 - 1));
    STATIC_REQUIRE(protoST::digestRunsUnmanaged(64 * 1024));
    STATIC_REQUIRE(protoST::digestRunsUnmanaged(std::size_t{1} << 40));
}

TEST_CASE("runDigest: a large input is digested unmanaged and a small one inline",
          "[bytes][gc]") {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, nullptr);
    const int outside = space.parkedThreads.load();
    auto parkedNow = [&] { return space.parkedThreads.load(); };

    CHECK(protoST::runDigest(&ctx, protoST::kUnmanagedDigestBytes - 1, "t", parkedNow) == outside);
    CHECK(protoST::runDigest(&ctx, protoST::kUnmanagedDigestBytes, "t", parkedNow) == outside + 1);
    CHECK(parkedNow() == outside);

    // A real digest over 64 KiB, as the sha256 primitive runs it.
    const std::string data(protoST::kUnmanagedDigestBytes, 'x');
    int parkedDuring = -1;
    const std::string hash = protoST::runDigest(&ctx, data.size(), "ByteArray sha256", [&] {
        parkedDuring = parkedNow();
        return protoio::digest::sha256(data);
    });
    CHECK(parkedDuring == outside + 1);
    CHECK(hash.size() == 32);
    CHECK(parkedNow() == outside);
}

TEST_CASE("runDigest: a protoio::Error is raised after the unmanaged region is left",
          "[bytes][gc]") {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, nullptr);
    const int outside = space.parkedThreads.load();
    int parkedWhenRaised = -1;
    try {
        protoST::runDigest(&ctx, protoST::kUnmanagedDigestBytes, "ByteArray sha256", [&]() -> int {
            throw protoio::Error(protoio::Error::Kind::Internal, "digest failed");
        });
        FAIL("runDigest did not raise");
    } catch (const protoST::ClassedErrorSignal& e) {
        parkedWhenRaised = space.parkedThreads.load();
        CHECK(std::string(e.className()) == "InternalError");
        CHECK(e.message() == "ByteArray sha256: digest failed");
    }
    CHECK(parkedWhenRaised == outside);
}
