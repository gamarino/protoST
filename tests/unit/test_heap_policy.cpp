// protoST's memory policy (STRuntime::configureHeap), decided per process from
// the environment when a runtime is created. Precedence, highest first:
//
//   1. PROTOCORE_ADAPTIVE_HEAP=1    the adaptive controller; its budget is
//                                   PROTOCORE_HEAP_LIMIT_CELLS when set
//                                   (protoCore applies this switch itself).
//   2. PROTOCORE_HEAP_LIMIT_CELLS   a fixed limit of exactly that many cells
//                                   (0: no limit at all).
//   3. PROTOCORE_ADAPTIVE_HEAP=0    the fixed limit protoST used by default up
//                                   to 0.6.1: 10M cells (640 MB), capped at a
//                                   quarter of physical memory.
//   4. none of them                 the adaptive controller (the default since
//                                   0.6.2), budget 75 % of physical memory or
//                                   of the process memory limit.
//
// Each case is its own process under ctest (catch_discover_tests), so the
// controller's process-wide budget never leaks from one case to the next.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "protoCore.h"
#include "PosixEnv.h"

#include <cstdlib>
#include <optional>
#include <string>

namespace {

// Sets (or, with nullptr, removes) a variable for the scope, then restores it.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = std::string(old);
        if (value) setenv(name, value, 1);
        else unsetenv(name);
    }
    ~ScopedEnv() {
        if (old_) setenv(name_, old_->c_str(), 1);
        else unsetenv(name_);
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
private:
    const char* name_;
    std::optional<std::string> old_;
};

} // namespace

TEST_CASE("HeapPolicy: by default the adaptive heap controller drives the heap",
          "[heap][policy]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", nullptr);
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", nullptr);
    protoST::STRuntime rt;
    const proto::AdaptiveHeapStats stats = rt.space()->adaptiveHeapStats();
    CHECK(stats.enabled);
    CHECK(stats.hardCells > 0);
    CHECK(stats.softCells > 0);
    CHECK(stats.softCells <= stats.hardCells);
    // The budget is protoCore's automatic one, not the old fixed 10M cells
    // (the controller's budget is 75 % of memory; no supported machine has
    // less than 1 GB).
    CHECK(stats.hardCells != 10'000'000);
}

TEST_CASE("HeapPolicy: PROTOCORE_ADAPTIVE_HEAP=0 restores the fixed 10M-cell limit",
          "[heap][policy]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", "0");
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", nullptr);
    protoST::STRuntime rt;
    proto::ProtoSpace* space = rt.space();
    CHECK_FALSE(space->adaptiveHeapStats().enabled);
    // 10M cells, or a quarter of physical memory on a smaller machine.
    CHECK(space->maxHeapSize > 0);
    CHECK(space->maxHeapSize <= 10'000'000);
    CHECK(space->softHeapLimit == 0);
}

TEST_CASE("HeapPolicy: PROTOCORE_HEAP_LIMIT_CELLS forces a fixed limit",
          "[heap][policy]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", nullptr);
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", "3000000");
    protoST::STRuntime rt;
    proto::ProtoSpace* space = rt.space();
    CHECK_FALSE(space->adaptiveHeapStats().enabled);
    CHECK(space->maxHeapSize == 3'000'000);
}

TEST_CASE("HeapPolicy: PROTOCORE_HEAP_LIMIT_CELLS=0 runs with no limit",
          "[heap][policy]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", nullptr);
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", "0");
    protoST::STRuntime rt;
    proto::ProtoSpace* space = rt.space();
    CHECK_FALSE(space->adaptiveHeapStats().enabled);
    CHECK(space->maxHeapSize == 0);
}

TEST_CASE("HeapPolicy: PROTOCORE_ADAPTIVE_HEAP=1 with a limit gives the controller that budget",
          "[heap][policy]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", "1");
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", "3000000");
    protoST::STRuntime rt;
    const proto::AdaptiveHeapStats stats = rt.space()->adaptiveHeapStats();
    CHECK(stats.enabled);
    CHECK(stats.hardCells == 3'000'000);
}

// ---------------------------------------------------------------------------
// Engine slot arrays are sized on demand (0.6.2)
// ---------------------------------------------------------------------------
//
// protoCore's stop-the-world root scan visits every slot of every thread's
// automaticLocals. Up to 0.6.1 each engine context was pre-sized to 1 M slots,
// so every pause scanned 1 M slots per thread (25-31 ms with twelve workers,
// benchmarks/reports/2026-10-12-adaptive-heap-default.md). The array now
// starts small and grows only as deep as the frames go.

#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/TransientPin.h"

#include <memory>
#include <vector>

namespace {

const proto::ProtoObject* runSource(protoST::STRuntime& rt,
                                    std::vector<std::unique_ptr<protoST::BytecodeModule>>& keep,
                                    const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    const proto::ProtoObject* r = rt.runTopLevel(*bc);
    keep.push_back(std::move(bc));
    return r;
}

} // namespace

TEST_CASE("EngineSlots: a context's slot array grows only as deep as its frames go",
          "[heap][slots]") {
    protoST::STRuntime rt;
    std::vector<std::unique_ptr<protoST::BytecodeModule>> keep;
    proto::ProtoContext* ctx = rt.rootCtx();

    const proto::ProtoObject* r = runSource(rt, keep, "| s | s := 0. 1 to: 1000 do: [:i | s := s + i]. s");
    REQUIRE(r->asLong(ctx) == 500500);
    // A shallow program: the array keeps its initial size, far below the
    // 1 M slots of 0.6.1.
    CHECK(ctx->getAutomaticLocalsCount() <= protoST::kInitialEngineSlots);

    // A 2,000-deep recursion needs more slots: the array grows, and the
    // program still computes the right result.
    r = runSource(rt, keep,
        "Object subclass: #Deep.\n"
        "Deep >> down: n\n"
        "  n = 0 ifTrue: [^ 0].\n"
        "  ^ 1 + (self down: n - 1)\n"
        "\n"
        "Deep new down: 2000\n");
    REQUIRE(r->asLong(ctx) == 2000);
    const unsigned int grown = ctx->getAutomaticLocalsCount();
    CHECK(grown > protoST::kInitialEngineSlots);
    CHECK(grown < protoST::kEngineSlotCapacity);
}
