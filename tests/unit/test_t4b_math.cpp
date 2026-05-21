// T4-b — regression pins for the mathematical protocol (Track 4, sub-slice b).
//
// The math operations are C++ primitives bootstrapped onto the shared
// `Number` prototype (transcendentals over libm, rounding, the pure /
// algebraic operations) plus class-side constants on `Float`. They are always
// available — no `Import` — so a unary send like `9 sqrt` or `30 factorial`
// resolves directly.
//
// These pin the source -> parse -> compile -> run behaviour. The two cases
// that matter most for exactness are `2 raisedTo: 100` and `30 factorial`:
// both must stay exact LargeIntegers (D20), never overflow a double.

#include <catch2/catch_all.hpp>

#include <string>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/Bootstrap.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

namespace {

const proto::ProtoObject* runSrc(protoST::STRuntime& rt, const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    return rt.runTopLevel(*bc);
}

std::string fmt(protoST::STRuntime& rt, const proto::ProtoObject* o) {
    return protoST::formatValue(rt, rt.rootCtx(), o);
}

} // namespace

// --- transcendental / irrational --------------------------------------------

TEST_CASE("T4-b: sqrt answers a Float", "[t4-b][math]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "9 sqrt.");
    REQUIRE(r->isFloat(rt.rootCtx()));
    REQUIRE(r->asDouble(rt.rootCtx()) == Catch::Approx(3.0));
}

TEST_CASE("T4-b: trig and inverse-trig", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "0 cos.")->asDouble(rt.rootCtx()) == Catch::Approx(1.0));
    REQUIRE(runSrc(rt, "0 sin.")->asDouble(rt.rootCtx()) == Catch::Approx(0.0));
    REQUIRE(runSrc(rt, "0 tan.")->asDouble(rt.rootCtx()) == Catch::Approx(0.0));
    REQUIRE(runSrc(rt, "1 arcSin.")->asDouble(rt.rootCtx())
            == Catch::Approx(1.5707963267948966));
    REQUIRE(runSrc(rt, "1 arcCos.")->asDouble(rt.rootCtx()) == Catch::Approx(0.0));
    REQUIRE(runSrc(rt, "0 arcTan.")->asDouble(rt.rootCtx()) == Catch::Approx(0.0));
}

TEST_CASE("T4-b: ln / exp / log / log:", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "1 ln.")->asDouble(rt.rootCtx()) == Catch::Approx(0.0));
    REQUIRE(runSrc(rt, "0 exp.")->asDouble(rt.rootCtx()) == Catch::Approx(1.0));
    REQUIRE(runSrc(rt, "100 log.")->asDouble(rt.rootCtx()) == Catch::Approx(2.0));
    REQUIRE(runSrc(rt, "8 log: 2.")->asDouble(rt.rootCtx()) == Catch::Approx(3.0));
}

TEST_CASE("T4-b: a domain error lets the libm result through", "[t4-b][math]") {
    protoST::STRuntime rt;
    // (-1) sqrt is NaN, 0 ln is -inf — no protoST Error is raised.
    REQUIRE(std::isnan(runSrc(rt, "(0 - 1) sqrt.")->asDouble(rt.rootCtx())));
    REQUIRE(std::isinf(runSrc(rt, "0 ln.")->asDouble(rt.rootCtx())));
}

// --- rounding ---------------------------------------------------------------

TEST_CASE("T4-b: floor / ceiling / rounded / truncated", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "3.7 floor.")->asLong(rt.rootCtx()) == 3);
    REQUIRE(runSrc(rt, "3.2 ceiling.")->asLong(rt.rootCtx()) == 4);
    REQUIRE(runSrc(rt, "3.5 rounded.")->asLong(rt.rootCtx()) == 4);
    REQUIRE(runSrc(rt, "3.7 truncated.")->asLong(rt.rootCtx()) == 3);
    // An integer receiver answers itself.
    REQUIRE(runSrc(rt, "5 floor.")->asLong(rt.rootCtx()) == 5);
}

// --- pure / algebraic -------------------------------------------------------

TEST_CASE("T4-b: sign / squared / reciprocal / isZero", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "-42 sign.")->asLong(rt.rootCtx()) == -1);
    REQUIRE(runSrc(rt, "0 sign.")->asLong(rt.rootCtx()) == 0);
    REQUIRE(runSrc(rt, "7 sign.")->asLong(rt.rootCtx()) == 1);
    REQUIRE(runSrc(rt, "6 squared.")->asLong(rt.rootCtx()) == 36);
    REQUIRE(runSrc(rt, "4 reciprocal.")->asDouble(rt.rootCtx())
            == Catch::Approx(0.25));
    REQUIRE(runSrc(rt, "0 isZero.") == PROTO_TRUE);
    REQUIRE(runSrc(rt, "1 isZero.") == PROTO_FALSE);
}

TEST_CASE("T4-b: min: / max: / between:and:", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "3 max: 7.")->asLong(rt.rootCtx()) == 7);
    REQUIRE(runSrc(rt, "7 min: 3.")->asLong(rt.rootCtx()) == 3);
    REQUIRE(runSrc(rt, "5 between: 1 and: 10.") == PROTO_TRUE);
    REQUIRE(runSrc(rt, "15 between: 1 and: 10.") == PROTO_FALSE);
}

TEST_CASE("T4-b: asFloat / asInteger", "[t4-b][math]") {
    protoST::STRuntime rt;
    auto* f = runSrc(rt, "3 asFloat.");
    REQUIRE(f->isFloat(rt.rootCtx()));
    REQUIRE(f->asDouble(rt.rootCtx()) == Catch::Approx(3.0));
    auto* i = runSrc(rt, "3.9 asInteger.");
    REQUIRE_FALSE(i->isFloat(rt.rootCtx()));
    REQUIRE(i->asLong(rt.rootCtx()) == 3);
}

TEST_CASE("T4-b: gcd: / lcm:", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "12 gcd: 18.")->asLong(rt.rootCtx()) == 6);
    REQUIRE(runSrc(rt, "4 lcm: 6.")->asLong(rt.rootCtx()) == 12);
}

// --- raisedTo: / factorial — the LargeInteger exercise ----------------------

TEST_CASE("T4-b: raisedTo: with a small integer exponent", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "2 raisedTo: 10.")->asLong(rt.rootCtx()) == 1024);
    REQUIRE(runSrc(rt, "5 raisedTo: 0.")->asLong(rt.rootCtx()) == 1);
}

TEST_CASE("T4-b: raisedTo: 100 stays an exact LargeInteger", "[t4-b][math][D20]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "2 raisedTo: 100.");
    REQUIRE(r->isInteger(rt.rootCtx()));
    REQUIRE(fmt(rt, r) == "1267650600228229401496703205376");
}

TEST_CASE("T4-b: a Float exponent goes through pow", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "9 raisedTo: 0.5.")->asDouble(rt.rootCtx())
            == Catch::Approx(3.0));
}

TEST_CASE("T4-b: factorial of 30 stays an exact LargeInteger", "[t4-b][math][D20]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "30 factorial.");
    REQUIRE(r->isInteger(rt.rootCtx()));
    REQUIRE(fmt(rt, r) == "265252859812191058636308480000000");
}

TEST_CASE("T4-b: factorial of small values", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "0 factorial.")->asLong(rt.rootCtx()) == 1);
    REQUIRE(runSrc(rt, "5 factorial.")->asLong(rt.rootCtx()) == 120);
}

// --- class-side Float constants ---------------------------------------------

TEST_CASE("T4-b: Float pi and Float e", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "Float pi.")->asDouble(rt.rootCtx())
            == Catch::Approx(3.141592653589793));
    REQUIRE(runSrc(rt, "Float e.")->asDouble(rt.rootCtx())
            == Catch::Approx(2.718281828459045));
}

// --- composition: sqrt then squared round-trips -----------------------------

TEST_CASE("T4-b: sqrt squared round-trips approximately", "[t4-b][math]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "9 sqrt squared.")->asDouble(rt.rootCtx())
            == Catch::Approx(9.0));
}
