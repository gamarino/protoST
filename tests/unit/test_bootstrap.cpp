#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

TEST_CASE("Bootstrap installs the five base prototypes", "[bootstrap]") {
    protoST::STRuntime rt;
    const auto& b = rt.bootstrap();
    REQUIRE(b.objectProto       != nullptr);
    REQUIRE(b.numberProto        != nullptr);
    REQUIRE(b.smallIntegerProto  != nullptr);
    REQUIRE(b.booleanProto       != nullptr);
    REQUIRE(b.stringProto        != nullptr);
    REQUIRE(b.blockProto         != nullptr);
}

TEST_CASE("Bootstrap installs the full prototype tree", "[bootstrap]") {
    protoST::STRuntime rt;
    const auto& b = rt.bootstrap();
    REQUIRE(b.largeIntegerProto != nullptr);
    REQUIRE(b.floatProto        != nullptr);
    REQUIRE(b.symbolProto       != nullptr);
    REQUIRE(b.nilProto          != nullptr);
}

TEST_CASE("Bootstrap binds protoCore prototype slots", "[bootstrap]") {
    protoST::STRuntime rt;
    const auto& b = rt.bootstrap();
    auto* sp = rt.space();
    // After bootstrap, the protoCore slots used by literal construction
    // should point at our Smalltalk prototypes so dispatch reaches them.
    REQUIRE(sp->smallIntegerPrototype == b.smallIntegerProto);
    REQUIRE(sp->stringPrototype       == b.stringProto);
    REQUIRE(sp->booleanPrototype      == b.booleanProto);
}
