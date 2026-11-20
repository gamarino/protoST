// S47 (review M1, protoST 0.14.2): the registration of a capped wait for a
// park (S43) is undone however the code between registering and parking
// leaves, an exception included. A registration left behind keeps
// parkedCapped raised and the slot's awaited Future recorded, so every
// settle, task and epoch end scans the slots and wakes a thread that is not
// parked. The case throws inside the window and checks the count.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <stdexcept>

TEST_CASE("S47: a capped park registration is undone when its window throws",
          "[actors][s43][s47]") {
    protoST::STRuntime rt;
    const proto::ProtoObject* awaited = rt.rootCtx()->fromLong(1);
    REQUIRE(rt.parkedCappedWaits() == 0);
    bool thrown = false;
    try {
        const auto park = rt.beginCappedPark(awaited);
        (void)park;
        throw std::runtime_error("a check between registering and parking failed");
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    REQUIRE(thrown);
    CHECK(rt.parkedCappedWaits() == 0);
}
