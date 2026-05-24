// MNT-b2 — regression pins for the bug-fix slice covering D3, D5 and D8
// (see docs/STATUS.md).
//
//  D3  an unresolved selector signals a catchable `MessageNotUnderstood`
//      (a subclass of `Error`) instead of aborting with a hard C++ error
//  D5  class-side methods (`ClassName class >> sel`) are isolated from
//      instances — an instance sending a class-side selector gets a
//      `doesNotUnderstand`
//  D8  a `^` in a block whose home method has already returned signals a
//      catchable `BlockCannotReturn` (a subclass of `Error`) instead of
//      aborting with a hard C++ error
//
// These pin the source -> parse -> compile -> run behaviour so a regression is
// caught here, close to the cause, even if the conformance runner is not run.

#include <catch2/catch_all.hpp>

#include <stdexcept>
#include <string>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/Bootstrap.h"
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

// The result's string value, or "" when it is not a string.
std::string asStr(protoST::STRuntime& rt, const proto::ProtoObject* o) {
    if (!o) return {};
    const proto::ProtoString* s = o->asString(rt.rootCtx());
    return s ? s->toStdString(rt.rootCtx()) : std::string();
}

} // namespace

// --- D3: doesNotUnderstand is a catchable MessageNotUnderstood --------------

TEST_CASE("MNT-b2 D3: an unknown selector is caught by on: Error do:",
          "[mnt-b2][D3]") {
    protoST::STRuntime rt;
    // The unresolved send `3 fooBar` raises MessageNotUnderstood; the
    // `on: Error do:` handler catches it (Error is its ancestor) and yields a
    // marker value instead of the run aborting.
    auto* r = runSrc(rt, "[ 3 fooBar ] on: Error do: [ :e | 99 ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("MNT-b2 D3: the caught exception carries an informative messageText",
          "[mnt-b2][D3]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt,
        "[ 3 fooBar ] on: Error do: [ :e | e messageText ].");
    REQUIRE(r != nullptr);
    // 2026-05-24 ergonomics: messageText is enriched with the
    // receiver's class name (SmallInteger for `3`). The original
    // "doesNotUnderstand: <sel>" form remains as a prefix.
    REQUIRE(asStr(rt, r) ==
            "doesNotUnderstand: fooBar (receiver class: SmallInteger)");
}

TEST_CASE("MNT-b2 D3: MessageNotUnderstood is nameable and catchable by name",
          "[mnt-b2][D3]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt,
        "[ 3 fooBar ] on: MessageNotUnderstood do: [ :e | 7 ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("MNT-b2 D3: an unhandled unknown selector still aborts the run",
          "[mnt-b2][D3]") {
    protoST::STRuntime rt;
    // With no handler, the default action throws — the previous uncaught
    // behaviour is preserved for the top level / REPL.
    REQUIRE_THROWS(runSrc(rt, "3 fooBar."));
}

// --- D5: class-side methods are isolated from instances ---------------------

TEST_CASE("MNT-b2 D5: a class-side method is reachable from the class object",
          "[mnt-b2][D5]") {
    protoST::STRuntime rt;
    const char* src =
        "Object subclass: #Counter instanceVariableNames: 'value'. "
        "Counter >> setValue: n value := n. "
        "Counter >> value ^ value. "
        "Counter class >> startingAt: n "
        "  | c | "
        "  c := self new. "
        "  c setValue: n. "
        "  ^ c. "
        "(Counter startingAt: 10) value.";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 10);
}

TEST_CASE("MNT-b2 D5: a class-side method is NOT reachable from an instance",
          "[mnt-b2][D5]") {
    protoST::STRuntime rt;
    // `Counter new classOnly` must be a doesNotUnderstand — class-side and
    // instance-side protocols are disjoint.
    const char* src =
        "Object subclass: #Counter. "
        "Counter class >> classOnly ^ 1. "
        "Counter new classOnly.";
    REQUIRE_THROWS(runSrc(rt, src));
}

TEST_CASE("MNT-b2 D5: an instance sending a class-side selector raises a "
          "catchable MessageNotUnderstood", "[mnt-b2][D5]") {
    protoST::STRuntime rt;
    const char* src =
        "Object subclass: #Counter. "
        "Counter class >> classOnly ^ 1. "
        "[ Counter new classOnly ] on: Error do: [ :e | 42 ].";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("MNT-b2 D5: instance-side methods still work on instances",
          "[mnt-b2][D5]") {
    protoST::STRuntime rt;
    const char* src =
        "Object subclass: #Box instanceVariableNames: 'v'. "
        "Box >> put: n v := n. "
        "Box >> get ^ v. "
        "Box class >> ofValue: n "
        "  | b | b := self new. b put: n. ^ b. "
        
        "b := Box ofValue: 5. "
        "b get.";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 5);
}

// --- D8: dead-home non-local return is a catchable BlockCannotReturn --------

TEST_CASE("MNT-b2 D8: a `^` whose home already returned is caught by "
          "on: Error do:", "[mnt-b2][D8]") {
    protoST::STRuntime rt;
    const char* src =
        "Object subclass: #Escaper. "
        "Escaper >> escapedBlock ^ [ ^ 1 ]. "
        
        "e := Escaper new. "
        "blk := e escapedBlock. "
        "[ blk value. 'not reached' ] on: Error do: [ :ex | 55 ].";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 55);
}

TEST_CASE("MNT-b2 D8: the caught BlockCannotReturn carries an informative "
          "messageText", "[mnt-b2][D8]") {
    protoST::STRuntime rt;
    const char* src =
        "Object subclass: #Escaper. "
        "Escaper >> escapedBlock ^ [ ^ 1 ]. "
        
        "e := Escaper new. "
        "blk := e escapedBlock. "
        "[ blk value ] on: Error do: [ :ex | ex messageText ].";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(asStr(rt, r) ==
            "non-local return: home method has already returned");
}

TEST_CASE("MNT-b2 D8: BlockCannotReturn is nameable and catchable by name",
          "[mnt-b2][D8]") {
    protoST::STRuntime rt;
    const char* src =
        "Object subclass: #Escaper. "
        "Escaper >> escapedBlock ^ [ ^ 1 ]. "
        
        "e := Escaper new. "
        "blk := e escapedBlock. "
        "[ blk value ] on: BlockCannotReturn do: [ :ex | 9 ].";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 9);
}

TEST_CASE("MNT-b2 D8: an unhandled dead-home return still aborts the run",
          "[mnt-b2][D8]") {
    protoST::STRuntime rt;
    const char* src =
        "Object subclass: #Escaper. "
        "Escaper >> escapedBlock ^ [ ^ 1 ]. "
        
        "e := Escaper new. "
        "blk := e escapedBlock. "
        "blk value.";
    REQUIRE_THROWS(runSrc(rt, src));
}

TEST_CASE("MNT-b2 D8: a live-home non-local return is unaffected",
          "[mnt-b2][D8]") {
    protoST::STRuntime rt;
    // `^` inside a block whose home method is still on the stack is an
    // ordinary non-local return — it must still work.
    const char* src =
        "Object subclass: #Finder instanceVariableNames: 'items'. "
        "Finder >> firstEven "
        "  #( 1 3 4 7 ) do: [ :x | x isEven ifTrue: [ ^ x ] ]. "
        "  ^ 0. "
        "Finder new firstEven.";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 4);
}
