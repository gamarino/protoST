// F8-2: DapTransport — Content-Length framed JSON over a pair of streams.
//
// These tests exercise the wire format without a real VS Code: a framed
// message is fed through std::istringstream and the round-trip through
// std::ostringstream is asserted.
#include <catch2/catch_all.hpp>
#include "dap/DapTransport.h"

#include <sstream>
#include <string>

using protoST::DapTransport;
using nlohmann::json;

namespace {
// Builds a DAP-framed message string for `body`.
std::string framed(const std::string& body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
}

TEST_CASE("DapTransport reads a single framed message", "[dap]") {
    std::istringstream in(framed(R"({"seq":1,"type":"request","command":"initialize"})"));
    std::ostringstream out;
    DapTransport t(in, out);

    auto msg = t.readMessage();
    REQUIRE(msg.has_value());
    CHECK((*msg)["seq"] == 1);
    CHECK((*msg)["type"] == "request");
    CHECK((*msg)["command"] == "initialize");
}

TEST_CASE("DapTransport reads multiple consecutive messages", "[dap]") {
    std::istringstream in(framed(R"({"seq":1,"command":"a"})") +
                          framed(R"({"seq":2,"command":"b"})"));
    std::ostringstream out;
    DapTransport t(in, out);

    auto m1 = t.readMessage();
    auto m2 = t.readMessage();
    auto m3 = t.readMessage();
    REQUIRE(m1.has_value());
    REQUIRE(m2.has_value());
    CHECK((*m1)["command"] == "a");
    CHECK((*m2)["command"] == "b");
    CHECK_FALSE(m3.has_value());  // EOF
}

TEST_CASE("DapTransport tolerates lone-LF header line endings", "[dap]") {
    std::string body = R"({"seq":7,"command":"x"})";
    std::istringstream in("Content-Length: " + std::to_string(body.size()) +
                          "\n\n" + body);
    std::ostringstream out;
    DapTransport t(in, out);

    auto msg = t.readMessage();
    REQUIRE(msg.has_value());
    CHECK((*msg)["seq"] == 7);
}

TEST_CASE("DapTransport returns nullopt on EOF", "[dap]") {
    std::istringstream in("");
    std::ostringstream out;
    DapTransport t(in, out);
    CHECK_FALSE(t.readMessage().has_value());
}

TEST_CASE("DapTransport returns nullopt on malformed JSON body", "[dap]") {
    std::istringstream in(framed("{not valid json"));
    std::ostringstream out;
    DapTransport t(in, out);
    CHECK_FALSE(t.readMessage().has_value());  // must not throw / crash
}

TEST_CASE("DapTransport returns nullopt on truncated body", "[dap]") {
    // Header claims 50 bytes but only a few are present.
    std::istringstream in("Content-Length: 50\r\n\r\n{\"seq\":1}");
    std::ostringstream out;
    DapTransport t(in, out);
    CHECK_FALSE(t.readMessage().has_value());
}

TEST_CASE("DapTransport writes a correctly framed message", "[dap]") {
    std::istringstream in("");
    std::ostringstream out;
    DapTransport t(in, out);

    json msg = {{"seq", 1}, {"type", "event"}, {"event", "initialized"}};
    t.writeMessage(msg);

    std::string s = out.str();
    std::string expectedBody = msg.dump();
    CHECK(s == "Content-Length: " + std::to_string(expectedBody.size()) +
                   "\r\n\r\n" + expectedBody);
}

TEST_CASE("DapTransport round-trips a written message back through a reader", "[dap]") {
    std::ostringstream out;
    {
        std::istringstream nin("");
        DapTransport writer(nin, out);
        writer.writeMessage({{"seq", 42}, {"type", "response"}, {"success", true}});
    }
    std::istringstream in(out.str());
    std::ostringstream sink;
    DapTransport reader(in, sink);

    auto msg = reader.readMessage();
    REQUIRE(msg.has_value());
    CHECK((*msg)["seq"] == 42);
    CHECK((*msg)["success"] == true);
}
