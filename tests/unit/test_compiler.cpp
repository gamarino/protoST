#include <catch2/catch_all.hpp>

#include <algorithm>
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

using protoST::Parser;
using protoST::Compiler;
using protoST::BytecodeModule;
using protoST::Op;

TEST_CASE("Compiler: empty module emits a single RETURN", "[compiler]") {
    Parser P("");
    auto ast = P.parseModule();
    Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->bytes().size() == 4);
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::RETURN_TOP);
}

TEST_CASE("Compiler: integer literal pushes from const pool", "[compiler]") {
    Parser P("42.");
    auto ast = P.parseModule();
    Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(bc->constInteger(0) == 42);
}

TEST_CASE("Compiler: nil/true/false push the dedicated opcodes", "[compiler]") {
    auto compile = [](const char* src) {
        Parser P(src);
        Compiler C; auto bc = C.compileModule(*P.parseModule());
        REQUIRE(!C.hasErrors());
        return bc;
    };
    REQUIRE(static_cast<Op>(compile("nil.")->bytes()[0])   == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(compile("true.")->bytes()[0])  == Op::PUSH_TRUE);
    REQUIRE(static_cast<Op>(compile("false.")->bytes()[0]) == Op::PUSH_FALSE);
}

TEST_CASE("Compiler: assignment creates slot and emits STORE_LOCAL", "[compiler]") {
    Parser P("x := 42. x.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // Sequence (STORE_LOCAL pops; DUP keeps assignment-as-expression value on stack):
    //   PUSH_CONST 0   (42)
    //   DUP
    //   STORE_LOCAL 0  (x)
    //   POP            (top-level statement separator)
    //   PUSH_LOCAL 0
    //   RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::DUP);
    REQUIRE(static_cast<Op>(bc->bytes()[4]) == Op::STORE_LOCAL);
    REQUIRE(bc->bytes()[5] == 0);
    REQUIRE(static_cast<Op>(bc->bytes()[6]) == Op::POP);
    REQUIRE(static_cast<Op>(bc->bytes()[8]) == Op::PUSH_LOCAL);
    REQUIRE(bc->bytes()[9] == 0);
    REQUIRE(static_cast<Op>(bc->bytes()[10]) == Op::RETURN_TOP);
}

TEST_CASE("Compiler: unary send emits SEND_UNARY", "[compiler]") {
    Parser P("nil printNl.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::SEND_UNARY);
    REQUIRE(bc->constSymbol(bc->bytes()[3]) == "printNl");
}

TEST_CASE("Compiler: binary `+` emits BIN_INT_ADD fast opcode (selector + still in operand)", "[compiler]") {
    // 2026-05-24 perf: `+` / `-` / `<=` / `<` / `>=` / `>` / `=` between
    // generic operands compile to the SmallInt-tagged fast opcodes that
    // fall back to SEND_BINARY on a non-Integer receiver (see b623448).
    // The selector const index is still in the operand so the cold path
    // can hand off to the shared SEND handler.
    Parser P("1 + 2.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_CONST 0 (1), PUSH_CONST 1 (2), BIN_INT_ADD (sym +)
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::PUSH_CONST);
    REQUIRE(static_cast<Op>(bc->bytes()[4]) == Op::BIN_INT_ADD);
    REQUIRE(bc->constSymbol(bc->bytes()[5]) == "+");
}

TEST_CASE("Compiler: non-fast binary send (`,` string concat) still emits SEND_BINARY", "[compiler]") {
    // Selectors outside the SmallInt fast-path set still take the
    // generic SEND_BINARY path — verifies the fast-opcode dispatch
    // table is selective, not blanket.
    Parser P("'a' , 'b'.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    REQUIRE(static_cast<Op>(bc->bytes()[4]) == Op::SEND_BINARY);
    REQUIRE(bc->constSymbol(bc->bytes()[5]) == ",");
}

TEST_CASE("Compiler: keyword send emits SEND_KEYWORD", "[compiler]") {
    Parser P("nil at: 1 put: 2.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_NIL, PUSH_CONST(1), PUSH_CONST(2), SEND_KEYWORD sym(at:put:)
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[6]) == Op::SEND_KEYWORD);
    REQUIRE(bc->constSymbol(bc->bytes()[7]) == "at:put:");
}

TEST_CASE("Compiler: cascade emits dup/pop dance and keeps last value", "[compiler]") {
    Parser P("nil yourself; printNl; size.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // At minimum: opcode sequence ends with the last SEND_UNARY (size) and not extra DUPs
    auto last_send = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        if (static_cast<Op>(bc->bytes()[i]) == Op::SEND_UNARY &&
            bc->constSymbol(bc->bytes()[i+1]) == "size") last_send = true;
    }
    REQUIRE(last_send);
}

TEST_CASE("Compiler: block compiles to a sub-module referenced by PUSH_BLOCK", "[compiler]") {
    Parser P("[ :a :b | a + b ].");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_BLOCK 0 ; RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_BLOCK);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(bc->numBlocks() == 1);
    auto& blk = bc->block(0);
    // body should end with RETURN_TOP after PUSH_LOCAL 0, PUSH_LOCAL 1, BIN_INT_ADD
    // (BIN_INT_ADD post-b623448 — the SmallInt fast opcode that falls back to
    // SEND_BINARY at runtime on non-Integer operands).
    REQUIRE(static_cast<Op>(blk.bytes()[0]) == Op::PUSH_LOCAL);
    REQUIRE(static_cast<Op>(blk.bytes()[2]) == Op::PUSH_LOCAL);
    REQUIRE(static_cast<Op>(blk.bytes()[4]) == Op::BIN_INT_ADD);
}

TEST_CASE("Compiler: F2 hero expression parses and compiles", "[compiler]") {
    Parser P("(1 to: 100) inject: 0 into: [:a :b | a + b].");
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->bytes().size() >= 8);                      // non-trivial program
    REQUIRE(bc->numBlocks() == 1);                          // the [:a :b| ...] block
    // ends with RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[bc->bytes().size()-2]) == Op::RETURN_TOP);
}

TEST_CASE("Compiler analysis: simple module-level capture", "[compiler][closures]") {
    Parser P("sum := 0. i := 1. [ i <= 100 ].");
    Compiler C; C.analyseClosures(*P.parseModule());
    const auto& a = C.analysis();
    // 'i' is declared at module level AND used in the inner block -> captured.
    // 'sum' is declared at module level but NOT used in any inner block -> NOT captured.
    REQUIRE(a.moduleCaptured.count("i") == 1);
    REQUIRE(a.moduleCaptured.count("sum") == 0);
}

TEST_CASE("Compiler analysis: no inner blocks -> nothing captured", "[compiler][closures]") {
    Parser P("x := 1. y := 2.");
    Compiler C; C.analyseClosures(*P.parseModule());
    REQUIRE(C.analysis().moduleCaptured.empty());
}

TEST_CASE("Compiler analysis: block-local args are not captured (they are block-private)", "[compiler][closures]") {
    Parser P("[ :a :b | a + b ].");
    Compiler C; C.analyseClosures(*P.parseModule());
    // 'a' and 'b' are block-local args, not module-level.
    REQUIRE(C.analysis().moduleCaptured.empty());
}

TEST_CASE("Compiler analysis: nested blocks bubble free vars up", "[compiler][closures]") {
    // Inner block uses 'x' which is declared at module level — through a nested block.
    Parser P("x := 0. [ [ x ] ].");
    Compiler C; C.analyseClosures(*P.parseModule());
    REQUIRE(C.analysis().moduleCaptured.count("x") == 1);
}

TEST_CASE("Compiler emit: captured var uses PUSH_CAPTURED / STORE_CAPTURED", "[compiler][closures]") {
    // `i` is module-local AND used in inner block → captured
    // Inner block has no own locals; references `i` → should emit PUSH_CAPTURED with const idx of symbol "i"
    Parser P("i := 0. [ i ].");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    // Find at least one STORE_CAPTURED in the top-level module bytecode
    bool sawStoreCaptured = false;
    bool sawPushCapturedInOuter = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        if (static_cast<Op>(bc->bytes()[i]) == Op::STORE_CAPTURED) sawStoreCaptured = true;
        if (static_cast<Op>(bc->bytes()[i]) == Op::PUSH_CAPTURED) sawPushCapturedInOuter = true;
    }
    REQUIRE(sawStoreCaptured);

    // Inside the block sub-module, find PUSH_CAPTURED with the symbol "i"
    REQUIRE(bc->numBlocks() == 1);
    auto& blk = bc->block(0);
    bool sawPushCapturedInBlock = false;
    uint8_t pushedSymIdx = 255;
    for (size_t i = 0; i + 1 < blk.bytes().size(); i += 2) {
        if (static_cast<Op>(blk.bytes()[i]) == Op::PUSH_CAPTURED) {
            sawPushCapturedInBlock = true;
            pushedSymIdx = blk.bytes()[i+1];
        }
    }
    REQUIRE(sawPushCapturedInBlock);
    REQUIRE(blk.constSymbol(pushedSymIdx) == "i");
}

TEST_CASE("Compiler emit: non-captured locals still use PUSH_LOCAL", "[compiler][closures]") {
    // `x` is module-local, no inner block uses it → NOT captured → slot-vector path
    Parser P("x := 7. x.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // Should see PUSH_LOCAL and STORE_LOCAL, NOT PUSH_CAPTURED
    bool sawPushCapturedAnywhere = false;
    bool sawStoreCapturedAnywhere = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        if (static_cast<Op>(bc->bytes()[i]) == Op::PUSH_CAPTURED)  sawPushCapturedAnywhere = true;
        if (static_cast<Op>(bc->bytes()[i]) == Op::STORE_CAPTURED) sawStoreCapturedAnywhere = true;
    }
    REQUIRE_FALSE(sawPushCapturedAnywhere);
    REQUIRE_FALSE(sawStoreCapturedAnywhere);
}

TEST_CASE("Compiler emit: block-local args do NOT trigger captured path", "[compiler][closures]") {
    // `a` and `b` are block-private args; should be PUSH_LOCAL within block, no PUSH_CAPTURED.
    Parser P("[ :a :b | a + b ].");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->numBlocks() == 1);
    auto& blk = bc->block(0);
    bool sawAnyCaptured = false;
    for (size_t i = 0; i + 1 < blk.bytes().size(); i += 2) {
        auto op = static_cast<Op>(blk.bytes()[i]);
        if (op == Op::PUSH_CAPTURED || op == Op::STORE_CAPTURED) sawAnyCaptured = true;
    }
    REQUIRE_FALSE(sawAnyCaptured);
}

TEST_CASE("Compiler: ClassDecl pre-pass collects ClassInfo", "[compiler][classes]") {
    Parser P("Object subclass: #Counter instanceVariableNames: 'value step'.");
    Compiler C; C.compileModule(*P.parseModule());
    REQUIRE(C.classes().count("Counter") == 1);
    const auto& info = C.classes().at("Counter");
    REQUIRE(info.name == "Counter");
    REQUIRE(info.superclassName == "Object");
    REQUIRE(info.instVarNames == std::vector<std::string>{"value", "step"});
}

TEST_CASE("Compiler: ClassDecl emits PUSH_GLOBAL/SEND/STORE_GLOBAL", "[compiler][classes]") {
    Parser P("Object subclass: #Counter.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    bool sawPushSuper = false, sawSendNewChild = false, sawStoreClass = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        Op op = static_cast<Op>(bc->bytes()[i]);
        uint8_t arg = bc->bytes()[i+1];
        if (op == Op::PUSH_GLOBAL  && bc->constSymbol(arg) == "Object")   sawPushSuper = true;
        // The class object comes from __subclassNamed: (a new child of the
        // superclass, or the existing class when it is re-declared).
        if (op == Op::SEND_KEYWORD && bc->constSymbol(arg) == "__subclassNamed:") sawSendNewChild = true;
        if (op == Op::STORE_GLOBAL && bc->constSymbol(arg) == "Counter")  sawStoreClass = true;
    }
    REQUIRE(sawPushSuper);
    REQUIRE(sawSendNewChild);
    REQUIRE(sawStoreClass);
}

// ---------------------------------------------------------------------------
// Inlined control blocks that declare temporaries (0.10.0). Their temporaries
// become hidden slots of the enclosing frame, as in Pharo; a block whose
// temporary a real block inside it captures stays a real block (each
// evaluation then needs its own binding).
namespace {
// The opcodes of a module's code, decoding EXTEND prefixes.
std::vector<Op> opcodesOf(const BytecodeModule& m) {
    std::vector<Op> ops;
    const auto& b = m.bytes();
    for (size_t i = 0; i + 1 < b.size(); i += 2) {
        const auto op = static_cast<Op>(b[i]);
        if (op != Op::EXTEND) ops.push_back(op);
    }
    return ops;
}
bool usesOp(const BytecodeModule& m, Op op) {
    for (Op o : opcodesOf(m)) if (o == op) return true;
    return false;
}
// The bytecode of method #m of class F declared in `src`: block 0 of the
// module, the only method.
std::unique_ptr<BytecodeModule> compileMethodModule(const std::string& src, Compiler& C) {
    Parser P("Object subclass: #F instanceVariableNames: 'iv'.\n" + src);
    auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->numBlocks() >= 1);
    return bc;
}
} // namespace

TEST_CASE("Compiler: control blocks with temporaries are inlined", "[compiler][inline]") {
    const char* bodies[] = {
        "F >> m: c\n  | n |\n  n := 0.\n  c ifTrue: [ | t | t := 1. n := t ].\n  ^ n\n",
        "F >> m: c\n  ^ c ifTrue: [ | t | t := 1. t ] ifFalse: [ | u | u := 2. u ]\n",
        "F >> m: c\n  ^ c and: [ | t | t := 1. t > 0 ]\n",
        "F >> m: c\n  | k |\n  k := 0.\n  [ | t | t := k. t < 5 ] whileTrue: [ | u | u := k + 1. k := u ].\n  ^ k\n",
        "F >> m: c\n  | s |\n  s := 0.\n  1 to: 3 do: [ :i | | t | t := i * 2. s := s + t ].\n  ^ s\n",
        "F >> m: c\n  ^ c ifNotNil: [ :x | | t | t := x. t ] ifNil: [ | u | u := 0. u ]\n",
    };
    for (const char* body : bodies) {
        INFO(body);
        Compiler C;
        auto bc = compileMethodModule(body, C);
        const BytecodeModule& method = bc->block(0);
        REQUIRE_FALSE(usesOp(method, Op::PUSH_BLOCK));
        REQUIRE_FALSE(usesOp(method, Op::MAKE_CAPTURED));
        REQUIRE_FALSE(usesOp(method, Op::STORE_CAPTURED));
        REQUIRE_FALSE(usesOp(method, Op::PUSH_CAPTURED));
    }
}

TEST_CASE("Compiler: an inlined block's temporary is reset to nil on every entry",
          "[compiler][inline]") {
    // RESET_LOCAL t precedes the body, inside the loop: the loop's back jump
    // lands before it.
    Compiler C;
    auto bc = compileMethodModule(
        "F >> m\n  | k |\n  k := 0.\n  [ k < 3 ] whileTrue: [ | t | t := k. k := t + 1 ].\n  ^ k\n", C);
    const auto ops = opcodesOf(bc->block(0));
    REQUIRE(std::find(ops.begin(), ops.end(), Op::RESET_LOCAL) != ops.end());
}

TEST_CASE("Compiler: sibling inlined blocks share the slots of their temporaries",
          "[compiler][inline]") {
    // Sixty sibling arms with one temporary each, then one with two: the
    // frame needs the slots of the deepest nesting only, not one per
    // temporary of the method.
    std::string src = "F >> m: n\n  | s |\n  s := 0.\n";
    for (int i = 0; i < 60; ++i)
        src += "  n < 0 ifTrue: [ | u" + std::to_string(i) + " | u" + std::to_string(i)
             + " := " + std::to_string(i) + ". s := u" + std::to_string(i) + " ].\n";
    src += "  n < 0 ifTrue: [ | a b | a := 1. b := 2. s := a + b ].\n  ^ s\n";
    Compiler C;
    auto bc = compileMethodModule(src, C);
    unsigned hidden = 0;
    for (const auto& name : bc->block(0).localNames())
        if (name.rfind("__bt_", 0) == 0) ++hidden;
    REQUIRE(hidden == 2);
}

TEST_CASE("Compiler: only the slots a de-optimised conditional shares are routed",
          "[compiler][inline]") {
    // `t` is used by the block literal of an inlined and:, whose copy shares
    // it when the receiver is not a Boolean: its accesses follow forwards.
    // `k` is not: its accesses stay plain PUSH_LOCAL / STORE_LOCAL.
    Compiler C;
    auto bc = compileMethodModule(
        "F >> m: r\n  | t k |\n  t := 1. k := 2.\n  ^ (r and: [ t > 0 ]) -> k\n", C);
    const auto& m = bc->block(0);
    const auto& names = m.localNames();
    auto slotOf = [&](const std::string& n) {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return i;
        FAIL("no slot " << n);
        return size_t{0};
    };
    const size_t t = slotOf("t"), k = slotOf("k");
    bool tShared = false, tPlain = false, kShared = false, kPlain = false;
    for (size_t pc = 0; pc + 1 < m.bytes().size(); pc += protoST::kInstrSize) {
        const auto op = static_cast<Op>(m.bytes()[pc]);
        const size_t arg = m.bytes()[pc + 1];
        const bool shared = op == Op::PUSH_SHARED_LOCAL || op == Op::STORE_SHARED_LOCAL;
        const bool plain = op == Op::PUSH_LOCAL || op == Op::STORE_LOCAL;
        if (arg == t) { tShared = tShared || shared; tPlain = tPlain || plain; }
        if (arg == k) { kShared = kShared || shared; kPlain = kPlain || plain; }
    }
    REQUIRE(tShared);
    REQUIRE_FALSE(tPlain);
    REQUIRE(kPlain);
    REQUIRE_FALSE(kShared);
}

TEST_CASE("Compiler: a control block whose temporary a block captures is not inlined",
          "[compiler][inline]") {
    Compiler C;
    auto bc = compileMethodModule(
        "F >> m\n  | bs |\n  bs := OrderedCollection new.\n"
        "  1 to: 3 do: [ :i | | t | t := i. bs add: [ t ] ].\n  ^ bs\n", C);
    // The to:do: body is a real block, which makes its own captured dict.
    REQUIRE(usesOp(bc->block(0), Op::PUSH_BLOCK));
}
