// MNT-c — regression pins for the numeric-tower slice (D11 + D20).
//
//  D11  Float arithmetic, comparison and mixed-mode (Integer <-> Float)
//       arithmetic are bound on the shared `Number` prototype, delegating to
//       protoCore's own promoting / coercing arithmetic.
//  D20  An integer computation that overflows the 56-bit inline SmallInteger
//       range promotes transparently to an exact arbitrary-precision
//       LargeInteger; its `printString` shows the full exact digits.
//
// These pin the source -> parse -> compile -> run behaviour so a regression is
// caught here, close to the cause.

#include <catch2/catch_all.hpp>

#include <string>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

namespace {

// Compile `src` and run it at top level, returning the module's result.
const proto::ProtoObject* runSrc(protoST::STRuntime& rt, const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    return rt.runTopLevel(*bc);
}

// The result rendered exactly through the numeric-tower formatter.
std::string fmt(protoST::STRuntime& rt, const proto::ProtoObject* o) {
    return protoST::formatValue(rt, rt.rootCtx(), o);
}

} // namespace

// --- D11: Float arithmetic --------------------------------------------------

TEST_CASE("MNT-c D11: Float addition yields a Float", "[mnt-c][D11]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "1.5 + 2.5.");
    REQUIRE(r != nullptr);
    REQUIRE(r->isFloat(rt.rootCtx()));
    REQUIRE(r->asDouble(rt.rootCtx()) == Catch::Approx(4.0));
    REQUIRE(fmt(rt, r) == "4.0");
}

TEST_CASE("MNT-c D11: Float subtraction / multiplication / division",
          "[mnt-c][D11]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "5.0 - 1.5.")->asDouble(rt.rootCtx()) == Catch::Approx(3.5));
    REQUIRE(runSrc(rt, "2.5 * 2.0.")->asDouble(rt.rootCtx()) == Catch::Approx(5.0));
    REQUIRE(runSrc(rt, "5.0 / 2.0.")->asDouble(rt.rootCtx()) == Catch::Approx(2.5));
}

TEST_CASE("MNT-c D11: Float ordered comparison answers a boolean",
          "[mnt-c][D11]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "1.5 < 2.5.")  == PROTO_TRUE);
    REQUIRE(runSrc(rt, "2.5 < 1.5.")  == PROTO_FALSE);
    REQUIRE(runSrc(rt, "2.5 >= 2.5.") == PROTO_TRUE);
    REQUIRE(runSrc(rt, "1.5 = 1.5.")  == PROTO_TRUE);
}

// --- D11: mixed-mode arithmetic ---------------------------------------------

TEST_CASE("MNT-c D11: Integer + Float coerces to a Float", "[mnt-c][D11]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "1 + 2.5.");
    REQUIRE(r->isFloat(rt.rootCtx()));
    REQUIRE(r->asDouble(rt.rootCtx()) == Catch::Approx(3.5));
}

TEST_CASE("MNT-c D11: Float + Integer coerces to a Float", "[mnt-c][D11]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "2.5 + 1.");
    REQUIRE(r->isFloat(rt.rootCtx()));
    REQUIRE(r->asDouble(rt.rootCtx()) == Catch::Approx(3.5));
}

TEST_CASE("MNT-c D11: dividing an Integer by a Float yields a Float",
          "[mnt-c][D11]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "1 / 2.0.");
    REQUIRE(r->isFloat(rt.rootCtx()));
    REQUIRE(r->asDouble(rt.rootCtx()) == Catch::Approx(0.5));
}

TEST_CASE("MNT-c D11: integer / integer is truncating division", "[mnt-c][D11]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "4 / 2.")->asLong(rt.rootCtx()) == 2);
    REQUIRE(runSrc(rt, "1 / 3.")->asLong(rt.rootCtx()) == 0);
    REQUIRE(runSrc(rt, "7 // 2.")->asLong(rt.rootCtx()) == 3);
}

TEST_CASE("MNT-c D11: the modulo operator answers the remainder",
          "[mnt-c][D11]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "7 \\\\ 3.")->asLong(rt.rootCtx()) == 1);
    REQUIRE(runSrc(rt, "10 \\\\ 5.")->asLong(rt.rootCtx()) == 0);
}

TEST_CASE("MNT-c D11: negated and abs", "[mnt-c][D11]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "5 negated.")->asLong(rt.rootCtx()) == -5);
    REQUIRE(runSrc(rt, "-7 abs.")->asLong(rt.rootCtx()) == 7);
    REQUIRE(runSrc(rt, "2.5 negated.")->asDouble(rt.rootCtx()) == Catch::Approx(-2.5));
}

TEST_CASE("MNT-c D11: dividing by zero signals a catchable ZeroDivide error",
          "[mnt-c][D11]") {
    protoST::STRuntime rt;
    // `/ 0` raises a native ZeroDivide, translated to a catchable `Error`
    // (messageText "ZeroDivide"). An `on: Error do:` guard catches it.
    auto* r = runSrc(rt,
        "[ 1 / 0 ] on: Error do: [ :e | 42 ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("MNT-c D11: Float printString shows a fractional part",
          "[mnt-c][D11]") {
    protoST::STRuntime rt;
    REQUIRE(fmt(rt, runSrc(rt, "(2.0 + 2.0).")) == "4.0");
    REQUIRE(fmt(rt, runSrc(rt, "3.14.")) == "3.14");
}

// --- D20: LargeInteger overflow promotion -----------------------------------

TEST_CASE("MNT-c D20: 25! overflows SmallInteger and stays exact",
          "[mnt-c][D20]") {
    protoST::STRuntime rt;
    // 25! is ~1.55e25, far past 2^56 (~7.2e16). Computed with a whileTrue:
    // loop; the result must be an exact LargeInteger, not a wrapped value.
    const char* src =
        "[ :limit | "
        "  | n f | "
        "  n := 1. f := 1. "
        "  [ n <= limit ] whileTrue: [ f := f * n. n := n + 1 ]. "
        "  f "
        "] value: 25.";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->isInteger(rt.rootCtx()));
    REQUIRE_FALSE(r->isFloat(rt.rootCtx()));
    // The exact value of 25! — must print in full, with no loss.
    REQUIRE(fmt(rt, r) == "15511210043330985984000000");
}

TEST_CASE("MNT-c D20: repeated doubling past SmallInteger stays exact",
          "[mnt-c][D20]") {
    protoST::STRuntime rt;
    // 2^80 = 1208925819614629174706176.
    const char* src =
        "[ :p | "
        "  | n acc | "
        "  n := 0. acc := 1. "
        "  [ n < p ] whileTrue: [ acc := acc * 2. n := n + 1 ]. "
        "  acc "
        "] value: 80.";
    auto* r = runSrc(rt, src);
    REQUIRE(r->isInteger(rt.rootCtx()));
    REQUIRE(fmt(rt, r) == "1208925819614629174706176");
}

TEST_CASE("MNT-c D20: a LargeInteger compares and negates exactly",
          "[mnt-c][D20]") {
    protoST::STRuntime rt;
    // (25! < 26!) and the negation of a LargeInteger prints with a sign.
    auto* lt = runSrc(rt,
        "([ :a | | n f | n := 1. f := 1. "
        "  [ n <= a ] whileTrue: [ f := f * n. n := n + 1 ]. f ] value: 25) "
        "< "
        "([ :a | | n f | n := 1. f := 1. "
        "  [ n <= a ] whileTrue: [ f := f * n. n := n + 1 ]. f ] value: 26).");
    REQUIRE(lt == PROTO_TRUE);

    auto* neg = runSrc(rt,
        "([ :a | | n f | n := 1. f := 1. "
        "  [ n <= a ] whileTrue: [ f := f * n. n := n + 1 ]. f ] value: 25) "
        "negated.");
    REQUIRE(fmt(rt, neg) == "-15511210043330985984000000");
}
