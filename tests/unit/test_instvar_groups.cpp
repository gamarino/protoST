// Instance-variable write groups.
//
// `a := x. b := y. c := z.` in a method is three STORE_INSTVAR, three
// publications of a new version of `self` into protoCore's mutable table.
// When nothing between the writes can run code or observe `self`, the
// compiler marks the run as a group (IVAR_GROUP ... IVAR_GROUP_END) and the
// engine publishes it as ONE version (ProtoObject::setAttributes, protoCore
// 2.11.0): read the current snapshot once, derive the new version, one
// compare-and-swap.  These tests pin down:
//
//   * which runs the compiler groups and which statements end a run;
//   * that every program answers what the per-write path answers, including
//     the cases where the engine must fall back (a SmallInteger fast path
//     that misses and sends a message that observes the receiver);
//   * the allocation saved per construction;
//   * that a reader on another thread sees all of a group or none of it.

#include <catch2/catch_all.hpp>
#include "PosixEnv.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

#include "protoST/STRuntime.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

namespace {

std::unique_ptr<protoST::BytecodeModule> compileSrc(const char* src, bool groups) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    C.setInstVarGroups(groups);
    auto bc = C.compileModule(*ast);
    if (C.hasErrors()) FAIL(C.errors().front());
    return bc;
}

// Runs `src` with groups on and with groups off (the per-write path) and
// checks that both answer the same SmallInteger; returns it.
long long runBoth(const char* src) {
    long long results[2] = {0, 0};
    for (int g = 0; g < 2; ++g) {
        auto bc = compileSrc(src, g == 1);
        protoST::STRuntime rt;
        const proto::ProtoObject* r = rt.runTopLevel(*bc);
        REQUIRE(r != nullptr);
        REQUIRE(r->isInteger(rt.rootCtx()));
        results[g] = r->asLong(rt.rootCtx());
    }
    CHECK(results[0] == results[1]);
    return results[1];
}

// Number of write groups in `m` and every module nested in it, and the sum of
// their sizes.
void countGroups(const protoST::BytecodeModule& m, int& groups, int& writes) {
    const auto& b = m.bytes();
    for (std::size_t pc = 0; pc + 1 < b.size();) {
        auto op = static_cast<protoST::Op>(b[pc]);
        unsigned int arg = b[pc + 1];
        pc += protoST::kInstrSize;
        while (op == protoST::Op::EXTEND && pc + 1 < b.size()) {
            op = static_cast<protoST::Op>(b[pc]);
            arg = (arg << 8) | b[pc + 1];
            pc += protoST::kInstrSize;
        }
        if (op == protoST::Op::IVAR_GROUP_END) {
            ++groups;
            writes += m.instVarGroupEntry(arg).count;
        }
    }
    for (std::size_t i = 0; i < m.numBlocks(); ++i) countGroups(m.block(i), groups, writes);
}

std::pair<int, int> groupsIn(const char* src) {
    auto bc = compileSrc(src, true);
    int groups = 0, writes = 0;
    countGroups(*bc, groups, writes);
    return {groups, writes};
}

// Methods are indented, top-level statements start at column 1 (a method
// body ends at the first unindented line; LANGUAGE.md section 3.3).
const char* kPoint = R"(Object subclass: #P instanceVariableNames: 'a b c d e'.
P >> a
    ^ a
P >> b
    ^ b
P >> c
    ^ c
P >> d
    ^ d
P >> e
    ^ e
)";

std::string withPoint(const char* rest) { return std::string(kPoint) + rest; }

} // namespace

// ---------------------------------------------------------------------------
// What the compiler groups
// ---------------------------------------------------------------------------

TEST_CASE("IvarGroups: consecutive assignments of inert values form one group",
          "[ivargroups]") {
    const auto src = withPoint(
R"(P >> init: x
    a := x. b := 2. c := 'three'. d := #four. e := nil.
0.)");
    CHECK(groupsIn(src.c_str()) == std::make_pair(1, 5));
}

TEST_CASE("IvarGroups: the first value may be anything; later ones must be inert",
          "[ivargroups]") {
    // `a := OrderedCollection new` runs before the group starts; the send in
    // `c := ...` ends the run, which then starts again at `c`.
    const auto src = withPoint(
R"(P >> init
    a := OrderedCollection new. b := 1. c := a size. d := 2. e := 3.
0.)");
    CHECK(groupsIn(src.c_str()) == std::make_pair(2, 5));
}

TEST_CASE("IvarGroups: a read of a variable the run already wrote ends the run",
          "[ivargroups]") {
    const auto src = withPoint(R"(P >> init
    a := 1. b := a + 1.
0.)");
    CHECK(groupsIn(src.c_str()) == std::make_pair(0, 0));
    const auto src2 = withPoint(R"(P >> init
    a := 1. b := c + 1.
0.)");
    CHECK(groupsIn(src2.c_str()) == std::make_pair(1, 2));
}

TEST_CASE("IvarGroups: globals, sends, blocks and nested assignments end a run",
          "[ivargroups]") {
    // A global can be undefined (a signalled Error); a block literal, a
    // keyword or unary send, a non-SmallInteger operator and an assignment
    // inside the value all end the run.
    const auto src = withPoint(
R"(P >> m1
    a := 1. b := Transcript.
P >> m2
    a := 1. b := [ 2 ].
P >> m3
    a := 1. b := 3 max: 4.
P >> m4
    a := 1. b := 3 * 4.
P >> m5: x
    | t |
    a := 1. b := t := x.
0.)");
    CHECK(groupsIn(src.c_str()) == std::make_pair(0, 0));
}

TEST_CASE("IvarGroups: temporaries and arguments are not instance variables",
          "[ivargroups]") {
    const auto src = withPoint(R"(P >> m: x
    | t |
    t := 1. a := x. b := t.
0.)");
    CHECK(groupsIn(src.c_str()) == std::make_pair(1, 2));
}

TEST_CASE("IvarGroups: switched off, the compiler emits no group", "[ivargroups]") {
    const auto src = withPoint(R"(P >> init
    a := 1. b := 2.
0.)");
    auto bc = compileSrc(src.c_str(), false);
    int groups = 0, writes = 0;
    countGroups(*bc, groups, writes);
    CHECK(groups == 0);
}

// ---------------------------------------------------------------------------
// Same answers as the per-write path
// ---------------------------------------------------------------------------

TEST_CASE("IvarGroups: an initializer stores every value", "[ivargroups]") {
    const auto src = withPoint(
R"(P >> initialize
    a := 1. b := 20. c := 300. d := 4000. e := 50000.
p := P new.
p a + p b + p c + p d + p e.)");
    CHECK(runBoth(src.c_str()) == 54321);
}

TEST_CASE("IvarGroups: arithmetic on the receiver's own variables", "[ivargroups]") {
    const auto src = withPoint(
R"(P >> initialize
    a := 0. b := 0.
P >> moveBy: n
    a := a + n. b := b - n. c := n.
p := P new.
1 to: 10 do: [:i | p moveBy: i].
(p a * 1000000) + (p b negated * 1000) + p c.)");
    CHECK(runBoth(src.c_str()) == 55 * 1000000 + 55 * 1000 + 10);
}

TEST_CASE("IvarGroups: a later write of the same name wins", "[ivargroups]") {
    const auto src = withPoint(R"(P >> m
    a := 1. b := 2. a := 3.
P new m; a.)");
    CHECK(groupsIn(src.c_str()).first == 1);
    CHECK(runBoth(src.c_str()) == 3);
}

TEST_CASE("IvarGroups: the last value is the statement's value", "[ivargroups]") {
    // In a block the last statement is the block's value.
    const auto src = withPoint(
R"(P >> m
    ^ [ a := 1. b := 7 ] value
P new m.)");
    CHECK(runBoth(src.c_str()) == 7);
}

TEST_CASE("IvarGroups: groups inside inlined conditionals and loops", "[ivargroups]") {
    const auto src = withPoint(
R"(P >> m: n
    a := 0. b := 0.
    1 to: n do: [:i | i even ifTrue: [a := a + i. c := i] ifFalse: [b := b + i. d := i]].
    ^ (a * 10000) + (b * 10) + c + d
P new m: 10.)");
    CHECK(runBoth(src.c_str()) == 30 * 10000 + 25 * 10 + 10 + 9);
}

TEST_CASE("IvarGroups: a SmallInteger operator that misses publishes the writes before its send",
          "[ivargroups]") {
    // `probe + 1` misses the SmallInteger fast path and sends #+ to a Probe,
    // whose method reads the receiver of the group: it must see `a := 10`,
    // written before the send, exactly as on the per-write path.
    const char* src = R"(Object subclass: #Probe instanceVariableNames: 'target'.
Probe >> target: t
    target := t
Probe >> + n
    ^ target a + n
Object subclass: #Q instanceVariableNames: 'a b c'.
Q >> a
    ^ a
Q >> b
    ^ b
Q >> c
    ^ c
Q >> fill: probe
    a := 10. b := probe + 1. c := 5.
q := Q new.
q fill: (Probe new target: q).
(q a * 10000) + (q b * 100) + q c.)";
    CHECK(groupsIn(src).first == 1);
    CHECK(runBoth(src) == 10 * 10000 + 11 * 100 + 5);
}

TEST_CASE("IvarGroups: SmallInteger overflow falls back to a send mid-group",
          "[ivargroups]") {
    const auto src = withPoint(
R"(P >> m: x
    a := 1. b := x + x. c := 3.
p := P new.
p m: 36028797018963967.
(p b - 36028797018963967 - 36028797018963967) + (p a * 10) + (p c * 100).)");
    CHECK(runBoth(src.c_str()) == 310);
}

TEST_CASE("IvarGroups: an error signalled in a run's value leaves the earlier writes stored",
          "[ivargroups]") {
    // A global that does not exist signals an Error.  The global ends the run
    // at compile time, so `a := 1` is written before the Error, as on the
    // per-write path, and the handler sees it.
    const auto src = withPoint(
R"(P >> m
    a := 1. b := NoSuchGlobalAnywhere.
p := P new.
[p m] on: Error do: [:ex | 0].
p a.)");
    CHECK(runBoth(src.c_str()) == 1);
}

TEST_CASE("IvarGroups: class-side instance variables", "[ivargroups]") {
    const char* src = R"(Object subclass: #K.
K class instanceVariableNames: 'x y'.
K class >> setX: ax y: ay
    x := ax. y := ay.
K class >> sum
    ^ x + y
K setX: 3 y: 4.
K sum.)";
    CHECK(groupsIn(src).first == 1);
    CHECK(runBoth(src) == 7);
}

// ---------------------------------------------------------------------------
// Allocation per construction
// ---------------------------------------------------------------------------

namespace {

// Cells taken from the space per `P new` with a five-variable initialize,
// measured as heapSize - freeCellsCount around `n` constructions.  Answers -1
// when a collection ran during the measurement.
double cellsPerConstruction(bool groups) {
    const char* src = R"(Object subclass: #P5 instanceVariableNames: 'id name qty price flag'.
P5 >> setId: i
    id := i. name := 'n'. qty := i + 1. price := 2. flag := true.
Object subclass: #Keep instanceVariableNames: 'last'.
Keep >> run: n
    1 to: n do: [:i | last := P5 new setId: i].
)";
    auto defs = compileSrc(src, groups);
    auto warm = compileSrc("Keep new run: 1000.", groups);
    auto loop = compileSrc("Keep new run: 50000.", groups);
    protoST::STRuntime rt;
    proto::ProtoSpace* space = rt.rootCtx()->space;
    rt.runTopLevel(*defs);
    rt.runTopLevel(*warm);
    const uint64_t cycles = space->getGCCycleCount();
    const long long before = (long long) space->heapSize - space->freeCellsCount;
    rt.runTopLevel(*loop);
    const long long after = (long long) space->heapSize - space->freeCellsCount;
    if (space->getGCCycleCount() != cycles) return -1.0;
    return double(after - before) / 50000.0;
}

} // namespace

TEST_CASE("IvarGroups: a grouped five-variable construction allocates fewer cells",
          "[ivargroups][cells]") {
    // No collection may run during the measurement.
    // Each runtime is measured twice, alternating, and the lower figure is
    // kept: the reading also moves with the heap's growth pattern (a later
    // runtime in the process measured up to 13 cells more for the same code).
    setenv("PROTOCORE_HEAP_LIMIT_CELLS", "100000000", 1);
    double perWrite = 1e9, grouped = 1e9;
    for (int round = 0; round < 2; ++round) {
        const double w = cellsPerConstruction(false);
        const double g = cellsPerConstruction(true);
        if (w < 0 || g < 0) {
            unsetenv("PROTOCORE_HEAP_LIMIT_CELLS");
            SKIP("a collection ran during the measurement");
        }
        WARN("round " << round << ": per write " << w << ", grouped " << g);
        perWrite = std::min(perWrite, w);
        grouped = std::min(grouped, g);
    }
    unsetenv("PROTOCORE_HEAP_LIMIT_CELLS");
    INFO("per write: " << perWrite << " cells, grouped: " << grouped << " cells");
    WARN("cells per construction: per write " << perWrite << ", grouped " << grouped);
    // Four of the five publications are spared (about 9 cells each).
    CHECK(grouped < perWrite - 20.0);
}

// ---------------------------------------------------------------------------
// Atomicity: a reader on another thread never sees part of a group
// ---------------------------------------------------------------------------

namespace {

constexpr int kRounds = 200000;
const proto::ProtoObject* gShared = nullptr;
const proto::ProtoString* gNames[3] = {nullptr, nullptr, nullptr};
std::atomic<bool> gWriterDone{false};
std::atomic<long> gReads{0};
std::atomic<long> gTorn{0};
std::atomic<long> gMidway{0};

// Takes one snapshot of the shared object's own attributes at a time and
// checks that its three variables carry the same round number.
const proto::ProtoObject* snapshotReader(proto::ProtoContext* c, const proto::ProtoObject*,
                                         const proto::ParentLink*, const proto::ProtoList*,
                                         const proto::ProtoSparseList*) {
    while (!gWriterDone.load(std::memory_order_acquire)) {
        const proto::ProtoSparseList* snap = gShared->getOwnAttributes(c);
        const proto::ProtoObject* v[3];
        for (int i = 0; i < 3; ++i)
            v[i] = snap->getAt(c, reinterpret_cast<uintptr_t>(gNames[i]));
        if (v[0] != v[1] || v[1] != v[2]) gTorn.fetch_add(1, std::memory_order_relaxed);
        else if (v[0] && v[0]->isInteger(c) && v[0]->asLong(c) > 0
                 && v[0]->asLong(c) < kRounds)
            gMidway.fetch_add(1, std::memory_order_relaxed);
        gReads.fetch_add(1, std::memory_order_relaxed);
    }
    return PROTO_NONE;
}

// Runs the writer loop on this thread while a protoCore thread reads
// snapshots; answers the number of torn snapshots.
long tornSnapshots(bool groups) {
    const char* defs = R"(Object subclass: #H instanceVariableNames: 'a b c'.
H >> initialize
    a := 0. b := 0. c := 0.
H >> set: k
    a := k. b := k. c := k.
H >> a
    ^ a
H new.)";
    auto d = compileSrc(defs, groups);
    auto loop = compileSrc("1 to: 200000 do: [:k | Shared set: k]. Shared a.", groups);
    protoST::STRuntime rt;
    proto::ProtoContext* ctx = rt.rootCtx();
    const proto::ProtoObject* h = rt.runTopLevel(*d);
    REQUIRE(h != nullptr);
    rt.globals()->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Shared"), h);
    gShared = h;
    gNames[0] = proto::ProtoString::createSymbol(ctx, "_iv_a");
    gNames[1] = proto::ProtoString::createSymbol(ctx, "_iv_b");
    gNames[2] = proto::ProtoString::createSymbol(ctx, "_iv_c");
    gWriterDone = false;
    gReads = 0;
    gTorn = 0;
    gMidway = 0;
    const proto::ProtoThread* reader = ctx->space->newThread(
        ctx, proto::ProtoString::createSymbol(ctx, "ivar-group-reader"), snapshotReader,
        nullptr, nullptr);
    const proto::ProtoObject* last = rt.runTopLevel(*loop);
    gWriterDone = true;
    const_cast<proto::ProtoThread*>(reader)->join(ctx);
    gShared = nullptr;
    REQUIRE(last->asLong(ctx) == kRounds);
    return gTorn.load();
}

} // namespace

TEST_CASE("IvarGroups: another thread sees all of a group or none of it",
          "[ivargroups][concurrency]") {
    const long perWriteTorn = tornSnapshots(false);
    const long perWriteReads = gReads.load();
    WARN("per-write path: " << perWriteTorn << " torn snapshots of " << perWriteReads);
    const long groupedTorn = tornSnapshots(true);
    INFO(gReads.load() << " snapshots, " << gMidway.load() << " taken mid-run");
    CHECK(groupedTorn == 0);
    // The reader must have run while the writer did, or nothing was shown.
    CHECK(gMidway.load() > 0);
}
