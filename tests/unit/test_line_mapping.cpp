// F8-1: pc-to-source-line mapping + sourceName plumbing.
//
// These tests verify the metadata the F8 DAP debugger relies on:
//   * BytecodeModule::lineForPc       — pc -> 1-based source line
//   * BytecodeModule::firstPcForLine  — line -> lowest breakable pc
//   * BytecodeModule::sourceName      — source file path, inherited by
//                                       sub-blocks (methods/blocks).
// Line mapping is purely additive metadata: it must not change emitted
// bytecode or runtime behaviour.
#include <catch2/catch_all.hpp>
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

using protoST::Parser;
using protoST::Compiler;
using protoST::BytecodeModule;
using protoST::Op;

namespace {
std::unique_ptr<BytecodeModule> compileSrc(const char* src) {
    Parser P(src);
    Compiler C;
    auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    return bc;
}
}  // namespace

TEST_CASE("F8-1: lineForPc maps instructions to source lines",
          "[compiler][f8][lines]") {
    // Three statements, one per line.
    const char* src =
        "1 + 2.\n"    // line 1
        "10 * 10.\n"  // line 2
        "100 - 1.\n"; // line 3
    auto bc = compileSrc(src);

    // instrLines_ must stay exactly bytes_.size() / 2 in length.
    REQUIRE(bc->instrLines().size() == bc->bytes().size() / 2);

    // Every instruction must report a non-zero line, and at least one
    // instruction must map to each of lines 1, 2 and 3.
    bool sawLine1 = false, sawLine2 = false, sawLine3 = false;
    for (size_t pc = 0; pc < bc->bytes().size(); pc += 2) {
        int line = bc->lineForPc(pc);
        if (line == 1) sawLine1 = true;
        if (line == 2) sawLine2 = true;
        if (line == 3) sawLine3 = true;
    }
    REQUIRE(sawLine1);
    REQUIRE(sawLine2);
    REQUIRE(sawLine3);

    // lineForPc out of range returns 0 (unknown).
    REQUIRE(bc->lineForPc(bc->bytes().size() + 100) == 0);
}

TEST_CASE("F8-1: firstPcForLine resolves a line to a breakable pc",
          "[compiler][f8][lines]") {
    const char* src =
        "1 + 2.\n"    // line 1
        "10 * 10.\n"  // line 2
        "100 - 1.\n"; // line 3
    auto bc = compileSrc(src);

    for (int line = 1; line <= 3; ++line) {
        size_t pc = bc->firstPcForLine(line);
        REQUIRE(pc != SIZE_MAX);
        REQUIRE(pc % 2 == 0);                  // pc is instruction-aligned
        REQUIRE(bc->lineForPc(pc) == line);    // round-trips
    }

    // firstPcForLine returns the LOWEST matching pc.
    size_t pc2 = bc->firstPcForLine(2);
    for (size_t pc = 0; pc < pc2; pc += 2) {
        REQUIRE(bc->lineForPc(pc) != 2);
    }

    // A line with no instruction yields the SIZE_MAX sentinel.
    REQUIRE(bc->firstPcForLine(999) == SIZE_MAX);
}

TEST_CASE("F8-1: sourceName is set and inherited by sub-blocks",
          "[compiler][f8][lines]") {
    // A class declaration plus a method — the method body compiles to a
    // sub-BytecodeModule.
    const char* src =
        "Object subclass: #Counter instanceVariableNames: 'value'. "
        "Counter >> bump ^ value + 1. ";
    auto bc = compileSrc(src);

    REQUIRE(bc->numBlocks() >= 1);

    // Default: no source name yet.
    REQUIRE(bc->sourceName().empty());

    // setSourceName stamps the top module AND recursively every sub-block.
    bc->setSourceName("counter.st");
    REQUIRE(bc->sourceName() == "counter.st");
    for (size_t i = 0; i < bc->numBlocks(); ++i) {
        REQUIRE(bc->block(i).sourceName() == "counter.st");
    }
}

TEST_CASE("F8-1: sub-block instructions carry their own source lines",
          "[compiler][f8][lines]") {
    const char* src =
        "Object subclass: #Counter instanceVariableNames: 'value'.\n"  // line 1
        "Counter >> bump\n"                                            // line 2
        "  ^ value + 1.\n";                                            // line 3
    auto bc = compileSrc(src);
    REQUIRE(bc->numBlocks() >= 1);

    const BytecodeModule& method = bc->block(0);
    REQUIRE(method.instrLines().size() == method.bytes().size() / 2);

    // The method body statement lives on line 3; at least one instruction
    // in the sub-block must map to it.
    REQUIRE(method.firstPcForLine(3) != SIZE_MAX);
}

// --- F8-4: local-slot names + module debug name ----------------------------
// The DAP Variables panel relies on BytecodeModule::localName(slot) to show
// real identifiers instead of "slot N". The compiler records the declared
// name per local slot; debugName labels the module in the call stack.

TEST_CASE("F8-4: module records its top-level local names",
          "[compiler][f8][locals]") {
    const char* src =
        "count := 1.\n"
        "total := count + 2.\n"
        "total.\n";
    auto bc = compileSrc(src);

    // Slot 0 = count, slot 1 = total (declaration order at module scope).
    REQUIRE(bc->localName(0) == "count");
    REQUIRE(bc->localName(1) == "total");
    // Out-of-range slot yields an empty name (caller falls back to "slot N").
    REQUIRE(bc->localName(99).empty());
    // The module carries a human label for the DAP call stack.
    REQUIRE(bc->debugName() == "<module>");
}

TEST_CASE("F8-4: a method's sub-module records self/args/locals by name",
          "[compiler][f8][locals]") {
    const char* src =
        "Object subclass: #Box instanceVariableNames: ''.\n"
        "Box >> compute: n\n"
        "  | total |\n"
        "  total := n + 100.\n"
        "  ^ total.\n";
    auto bc = compileSrc(src);
    REQUIRE(bc->numBlocks() >= 1);

    const BytecodeModule& method = bc->block(0);
    // Method scope: slot 0 = self, slot 1 = first arg, then locals.
    REQUIRE(method.localName(0) == "self");
    REQUIRE(method.localName(1) == "n");
    REQUIRE(method.localName(2) == "total");
    // The method module is labelled "<class>>><selector>".
    REQUIRE(method.debugName() == "Box>>compute:");
}
