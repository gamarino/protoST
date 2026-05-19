#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include <string_view>

TEST_CASE("versionString returns the expected prefix", "[sanity]") {
    std::string_view v = protoST::versionString();
    REQUIRE(v.starts_with("protoST"));
}
