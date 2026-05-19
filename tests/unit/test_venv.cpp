#include <catch2/catch_all.hpp>
#include "runtime/Venv.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("Venv create lays out directories and stenv.cfg", "[venv]") {
    auto tmp = fs::temp_directory_path() / "protost_test_venv_create";
    fs::remove_all(tmp);
    fs::create_directory(tmp);
    auto venv = tmp / ".venv";

    int rc = protoST::venvCreate(venv.string(), "/usr/local/bin", "0.1.0");
    REQUIRE(rc == 0);
    REQUIRE(fs::is_directory(venv));
    REQUIRE(fs::is_directory(venv / "bin"));
    REQUIRE(fs::is_directory(venv / "lib" / "protoST" / "modules"));
    REQUIRE(fs::is_directory(venv / "cache" / "bytecode"));
    REQUIRE(fs::is_regular_file(venv / "stenv.cfg"));
    REQUIRE(fs::is_regular_file(venv / "bin" / "activate"));

    // creating into an existing venv path should refuse
    REQUIRE(protoST::venvCreate(venv.string(), "/usr/local/bin", "0.1.0") != 0);

    fs::remove_all(tmp);
}
