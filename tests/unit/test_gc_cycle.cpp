// S15 — a collection cycle must complete AND reclaim what the interpreter
// made garbage.
//
// protoCore reclaims nothing a context has not submitted. Every cell a context
// allocates is chained onto that context's young generation; the collector's
// root scan records the chain head and marks the whole chain, so an unsubmitted
// chain is a set of cells that are live by definition. A context submits its
// chain when it is destroyed, or from `ProtoContext::safepoint()` once it has
// passed `maxAllocatedCellsPerContext`.
//
// protoST creates no `ProtoContext` of its own — a program runs on the
// runtime's root context, an actor turn on its worker's root context, and both
// live as long as the process. Until `ExecutionEngine::gcSafepoint` was added
// at the loop back-edge and at engine entry, the interpreter called
// `safepoint()` nowhere, so the root context's chain was never submitted: every
// cell a protoST program ever allocated stayed permanently live and every cycle
// reclaimed exactly zero. That is bug S15.
//
// The first test runs an allocating loop, then forces collections with the main
// thread parked, and asserts both halves of the claim: the cycle count advanced
// and a cycle reclaimed cells. Run the binary with `PROTOST_NO_GC_SAFEPOINT=1`
// to disable the hook; the reclaim assertion then fails with `reclaimed == 0`
// and a heap that grew from 1,048,576 to 3,866,624 cells, which is the pre-fix
// behaviour.


#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/BlockCode.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct GcHost {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;

    const proto::ProtoObject* run(const std::string& src) {
        protoST::Parser P(src.c_str());
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        REQUIRE(!C.hasErrors());
        const proto::ProtoObject* r = rt.runTopLevel(*bc);
        kept.push_back(std::move(bc));
        return r;
    }
};

// heapSize and freeCellsCount are plain ints that protoCore writes
// atomically (relaxedFetchAdd/relaxedStore, proto_internal.h) from the
// collector thread; a reader outside its mutex loads them the same way.
int relaxedRead(int& v) {
    return __atomic_load_n(&v, __ATOMIC_RELAXED);
}

// Ask for collections and wait for them, with this thread parked.
//
// Parking matters: protoCore's Phase 1 waits until every registered thread is
// parked, and a thread sitting in native C++ outside an unmanaged region is not
// parked — it would stall the very cycle it is waiting for. An idle worker is
// already parked (its semaphore wait is an unmanaged region), so the main
// thread is the only one that has to do anything here.
//
// `triggerGC()` is advisory: it raises the request only when the heap is short
// of free cells or a request is already pending, so it is re-issued on each
// pass rather than once.
//
// The caller asks for several cycles rather than one because protoCore frees a
// dead *mutable* object's cells one cycle after the cycle that found it
// unreachable: the first cycle only releases its entry from the mutables tree
// (Phase 5b), and the entry is a root while it is there. Every protoST Array is
// a mutable object, so a single cycle over a heap of dead arrays can
// legitimately reclaim nothing.
//
// Every cycle is observed, wherever it ran: a cycle can also start while a
// program runs (under the adaptive heap controller, protoST's default since
// 0.6.2, pacing starts one when the allocation rate calls for it), and its
// reclaim is published in `reclaimedLastCycle` only until the next cycle ends.
// The observer is therefore consulted after every program as well as while
// waiting, and keeps the largest per-cycle reclaim it saw and a log of every
// observation for the failure message. (Before 0.6.2 the observation was reset
// at each wait, so a cycle that ran during a program was never counted; under
// the controller on a CI runner that made the test fail with "best cycle
// reclaimed 0" although the heap had been collected.)
struct CycleObserver {
    proto::ProtoSpace* sp;
    uint64_t           seen;
    proto::proto_ulong maxReclaimed = 0;
    std::string        log;

    explicit CycleObserver(proto::ProtoSpace* s) : sp(s), seen(s->getGCCycleCount()) {}

    // Answers how many cycles completed since the last observation.
    uint64_t observe(const char* where) {
        const uint64_t now = sp->getGCCycleCount();
        if (now == seen) return 0;
        const uint64_t ran = now - seen;
        seen = now;
        const proto::proto_ulong r = sp->reclaimedLastCycle.load(std::memory_order_relaxed);
        if (r > maxReclaimed) maxReclaimed = r;
        const proto::AdaptiveHeapStats a = sp->adaptiveHeapStats();
        log += std::string("\n  ") + where + ": cycle " + std::to_string(now)
             + (ran > 1 ? " (" + std::to_string(ran) + " since last look)" : std::string())
             + ", reclaimed " + std::to_string(r)
             + ", live " + std::to_string(sp->liveCellsLastCycle.load(std::memory_order_relaxed))
             + ", heap " + std::to_string(relaxedRead(sp->heapSize))
             + ", free " + std::to_string(relaxedRead(sp->freeCellsCount))
             + (a.enabled ? ", soft limit " + std::to_string(a.softCells) : std::string(", fixed limit"));
        return ran;
    }
};

// Ask for one more collection and wait for it, with this thread parked.
// Answers whether a cycle completed within the budget. triggerGC starts a
// cycle only when the heap is short of free cells, so a request that is not
// answered is followed by a little more garbage (`pressure`) and asked
// again: how much garbage a round of the program leaves depends on the
// interpreter (0.10.0's Array new: allocates far less), and the test must
// not depend on it to get its collection.
bool forceCycle(protoST::STRuntime& rt, CycleObserver& obs, std::chrono::seconds budget,
                const std::function<void()>& pressure) {
    proto::ProtoSpace* sp = rt.space();
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        sp->triggerGC();
        {
            proto::ProtoContext::UnmanagedScope parked(rt.rootCtx());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (obs.observe("waiting") > 0) return true;
        pressure();
    }
    return false;
}

} // namespace

TEST_CASE("S15: a forced collection completes and reclaims the interpreter's garbage",
          "[gc]") {
    GcHost h;
    proto::ProtoSpace* sp = h.rt.space();

    const uint64_t cyclesBefore = sp->getGCCycleCount();
    CycleObserver obs(sp);
    const proto::proto_long     heapAfterFirstRound = [&] {
        // 20,000 eight-element arrays, none of them kept. Nothing this program
        // allocates is reachable when it returns, so a cycle that reclaims
        // nothing is a cycle that is treating the interpreter's young
        // generation as live — which is what S15 was.
        const proto::ProtoObject* r = h.run(
            "n := 0. "
            "1 to: 20000 do: [ :i | | a | a := Array new: 8. a at: 1 put: i. "
            "                        n := n + 1 ]. "
            "n.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(h.rt.rootCtx()) == 20000);
        obs.observe("first program");
        return static_cast<proto::proto_long>(relaxedRead(sp->heapSize));
    }();

    // Four rounds of "allocate a heap's worth, then collect it". triggerGC
    // starts a cycle only when the heap is short of free cells, so each round
    // re-creates the pressure; an idle process has, by protoCore's design, no
    // reason to collect at all.
    const auto pressure = [&] { h.run("1 to: 5000 do: [ :i | Array new: 8 ]. 0."); };
    forceCycle(h.rt, obs, std::chrono::seconds(20), pressure);
    for (int round = 0; round < 4; ++round) {
        h.run("1 to: 20000 do: [ :i | Array new: 8 ]. 0.");
        obs.observe("after a program");
        forceCycle(h.rt, obs, std::chrono::seconds(20), pressure);
    }

    const uint64_t cyclesAfter = sp->getGCCycleCount();

    INFO("cycles " << cyclesBefore << " -> " << cyclesAfter
         << ", best cycle reclaimed " << obs.maxReclaimed << " cells, heap "
         << heapAfterFirstRound << " -> " << relaxedRead(sp->heapSize) << " cells; observed:" << obs.log);

    // 1. A cycle completes.
    REQUIRE(cyclesAfter > cyclesBefore);
    // 2. It reclaims. This is the half that was zero before the fix, in every
    //    cycle, however many ran.
    REQUIRE(obs.maxReclaimed > 0);
    // 3. And reclaiming keeps the heap bounded: four more rounds of the same
    //    garbage must not need a bigger heap than the first round did. Before
    //    the fix the heap grew by roughly the round's allocation every time.
    REQUIRE(static_cast<proto::proto_long>(relaxedRead(sp->heapSize)) <= heapAfterFirstRound * 2);
}

// I2 (0.10.0): the closures over one sub-block share one `__bc_ptr__` cell,
// created with the first and anchored in the runtime's live registry
// (src/runtime/BlockCode.h). Here every closure is created and dropped inside a
// method, so once it returns only that anchor keeps the cell alive.
//
// A collected cell cannot be recognised by reading it (the sweep leaves an
// external pointer's fields in place, and the tag is in the pointer), so death
// is observed through protoCore's finalizer contract: the shared cell counts
// its finalization (detail::g_sharedCodeCellsFinalized), and a positive
// control -- an unanchored external pointer with a counting finalizer, made
// and dropped in the same way -- proves the harness does collect a dead one.
namespace {
std::atomic<int> g_controlFinalized{0};
int g_controlTarget = 0;

void countControlFinalized(void*) { g_controlFinalized.fetch_add(1, std::memory_order_relaxed); }

// `Probe drop`: makes an external pointer nothing references.
const proto::ProtoObject* dropExternalPointer(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                              const proto::ParentLink*, const proto::ProtoArguments*,
                                              const proto::ProtoKeywordArguments*) {
    ctx->fromExternalPointer(&g_controlTarget, &countControlFinalized);
    return PROTO_NONE;
}
} // namespace

TEST_CASE("The shared code cell of a sub-block is anchored by its runtime and only by it",
          "[gc][closure]") {
    const std::uint64_t before =
        protoST::detail::g_sharedCodeCellsFinalized.load(std::memory_order_relaxed);
    const int controlBefore = g_controlFinalized.load(std::memory_order_relaxed);
    {
        GcHost h;
        auto* ctx = h.rt.rootCtx();
        auto* probe = h.rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/true);
        probe->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "drop"),
                            ctx->fromMethod(nullptr, &dropExternalPointer));
        h.rt.globals()->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Probe"), probe);
        // The runtime owns the module, so its closures share one anchored cell.
        {
            protoST::Parser P("Object subclass: #Maker.\n"
                              "Maker >> churn: n\n"
                              "  | s |\n"
                              "  s := 0.\n"
                              "  1 to: n do: [:i | s := s + ([:y | y + i] value: 1). Probe drop].\n"
                              "  ^ s");
            auto ast = P.parseModule();
            REQUIRE(P.errors().empty());
            protoST::Compiler C;
            auto bc = C.compileModule(*ast);
            REQUIRE(!C.hasErrors());
            h.rt.runTopLevel(h.rt.adoptModule(std::move(bc)));
        }
        REQUIRE(h.run("Maker new churn: 100.")->asLong(ctx) == 5150);

        proto::ProtoSpace* sp = h.rt.space();
        CycleObserver obs(sp);
        const auto pressure = [&] { h.run("1 to: 5000 do: [ :i | Array new: 8 ]. 0."); };
        for (int round = 0; round < 3; ++round) {
            h.run("1 to: 20000 do: [ :i | Array new: 8 ]. 0.");
            REQUIRE(forceCycle(h.rt, obs, std::chrono::seconds(20), pressure));
        }
        // The control: the harness collects a dead external pointer.
        REQUIRE(g_controlFinalized.load(std::memory_order_relaxed) > controlBefore);
        // The shared cell was not collected: the anchor kept it.
        REQUIRE(protoST::detail::g_sharedCodeCellsFinalized.load(std::memory_order_relaxed) ==
                before);
        REQUIRE(h.run("Maker new churn: 10.")->asLong(ctx) == 65);
    }
}

// The anchor lives exactly as long as the runtime: in a host's space, which
// outlives the protoST runtime it hosts, no shared cell is collected while the
// runtime lives, and the collections after it is destroyed collect them --
// the kernel's (its files are modules the runtime owns) and `Maker`'s. (A
// space's own teardown runs no finalizers, so a runtime that owns its space
// cannot show this.)
TEST_CASE("The shared code cell of a sub-block is released with its runtime", "[gc][closure]") {
    const std::uint64_t before =
        protoST::detail::g_sharedCodeCellsFinalized.load(std::memory_order_relaxed);
    proto::ProtoSpace host;
    proto::ProtoContext hostCtx(&host, nullptr);
    {
        protoST::STRuntime st(host);
        protoST::Parser P("Object subclass: #Maker.\n"
                          "Maker >> churn: n\n"
                          "  | s |\n"
                          "  s := 0.\n"
                          "  1 to: n do: [:i | s := s + ([:y | y + i] value: 1)].\n"
                          "  ^ s.\n"
                          "Maker new churn: 100.");
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        REQUIRE(!C.hasErrors());
        REQUIRE(st.runTopLevel(st.adoptModule(std::move(bc)))->asLong(st.rootCtx()) == 5150);
        REQUIRE(protoST::detail::g_sharedCodeCellsFinalized.load(std::memory_order_relaxed) ==
                before);
    }
    const uint64_t cycles = host.getGCCycleCount();
    for (int round = 0;
         round < 200 &&
         protoST::detail::g_sharedCodeCellsFinalized.load(std::memory_order_relaxed) == before;
         ++round) {
        host.triggerGC();
        for (int i = 0; i < 20000; ++i)
            (void)hostCtx.newList()->appendLast(&hostCtx, hostCtx.fromInteger(i));
        hostCtx.safepoint();
        std::this_thread::yield();
    }
    INFO("cycles " << cycles << " -> " << host.getGCCycleCount());
    REQUIRE(protoST::detail::g_sharedCodeCellsFinalized.load(std::memory_order_relaxed) >
            before);
}
