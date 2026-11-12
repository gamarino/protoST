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
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

#include <chrono>
#include <cstdlib>
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
    {
        // 20,000 eight-element arrays, none of them kept, each made in an
        // activation of a real block (S15 was about the garbage of block
        // activations: the block, its frame and its captured dict were never
        // submitted). The block is held in a variable and sent value:, so the
        // compiler cannot inline it. Nothing this program allocates is
        // reachable when it returns, so a cycle that reclaims nothing is a
        // cycle that is treating the interpreter's young generation as live
        // -- which is what S15 was.
        const proto::ProtoObject* r = h.run(
            "n := 0. "
            "blk := [ :i | | a | a := Array new: 8. a at: 1 put: i. a ]. "
            "1 to: 20000 do: [ :i | blk value: i. n := n + 1 ]. "
            "n.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(h.rt.rootCtx()) == 20000);
        obs.observe("first program");
    }

    // Five rounds of "allocate a heap's worth, then collect it". triggerGC
    // starts a cycle only when the heap is short of free cells, so each round
    // re-creates the pressure; an idle process has, by protoCore's design, no
    // reason to collect at all. The heap after the first round is the
    // reference: the same round repeated four more times must not need more
    // than twice that heap. Before the fix every round added roughly its own
    // allocation to the heap. The reference is taken from the rounds
    // themselves, not from a program, so it does not depend on how much a
    // program allocates (the compiler's inlining changes that).
    const auto pressure = [&] { h.run("1 to: 5000 do: [ :i | Array new: 8 ]. 0."); };
    forceCycle(h.rt, obs, std::chrono::seconds(20), pressure);
    proto::proto_long heapAfterFirstRound = 0;
    for (int round = 0; round < 5; ++round) {
        h.run("1 to: 20000 do: [ :i | Array new: 8 ]. 0.");
        obs.observe("after a program");
        forceCycle(h.rt, obs, std::chrono::seconds(20), pressure);
        if (round == 0)
            heapAfterFirstRound = static_cast<proto::proto_long>(relaxedRead(sp->heapSize));
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
    // 3. And reclaiming keeps the heap bounded.
    REQUIRE(static_cast<proto::proto_long>(relaxedRead(sp->heapSize)) <= heapAfterFirstRound * 2);
}

// S3: a thread running an allocation-free loop must not stall a collection.
//
// protoCore starts a requested collection only after every running thread
// parks, and a thread that never allocates never reaches allocCell's poll; the
// interpreter parks at every loop back-edge (ExecutionEngine::gcSafepoint).
// An actor allocates without end under a small heap ceiling, so collections
// are requested again and again, while the main thread spins in a loop that
// allocates nothing. The program reads the collector's state through two
// primitives this test binds: `GcProbe gcCycles` (the completed cycles) and
// `GcProbe stopTheWorldRequested` (a collection waits for every thread to
// park).
//
// The case runs in both modes of the hook, read from the environment as the
// engine reads it (ctest runs it once without and once with
// PROTOST_NO_GC_SAFEPOINT=1, tests/CMakeLists.txt). Both outcomes are
// decided by the order of events, never by elapsed time:
//   * with the hook, the spin waits for a cycle to complete, which can only
//     happen while it spins if the loop parks; without the hook that loop
//     would never end;
//   * without it, the spin first waits until a collection is requested (it
//     stays requested: nothing the main thread does parks it), then runs a
//     fixed number of back-edges: no cycle can complete during them, so the
//     count is unchanged, and once the loop is over the program waits for
//     the actor, which parks the main thread and lets the collections run.
namespace {

const proto::ProtoObject* primGcCycles(protoST::STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject*,
                                       const proto::ProtoObject* const*, int) {
    return ctx->fromLong(static_cast<long long>(rt.space()->getGCCycleCount()));
}

const proto::ProtoObject* primStopTheWorldRequested(protoST::STRuntime& rt, proto::ProtoContext*,
                                                    const proto::ProtoObject*,
                                                    const proto::ProtoObject* const*, int) {
    return rt.space()->stwFlag.load() ? PROTO_TRUE : PROTO_FALSE;
}

} // namespace

TEST_CASE("S3: an allocation-free loop does not stall a collection", "[gc]") {
    const bool hookOff = std::getenv("PROTOST_NO_GC_SAFEPOINT") != nullptr;
    GcHost h;
    // A small soft limit, so the actor's garbage requests collections again
    // and again, under a hard ceiling far above it, so that the hook-off run
    // (whose collections reclaim nothing, S15) never reaches out-of-memory.
    // Set on the space directly: mutating the environment while worker
    // threads may read it is a data race.
    h.rt.space()->setHeapLimits(400000, 8000000);
    proto::ProtoContext* ctx = h.rt.rootCtx();
    const proto::ProtoObject* probe =
        h.rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/true);
    protoST::bindPrimitive(h.rt, probe, "gcCycles", h.rt.registry().registerPrim(primGcCycles));
    protoST::bindPrimitive(h.rt, probe, "stopTheWorldRequested",
                           h.rt.registry().registerPrim(primStopTheWorldRequested));
    const_cast<proto::ProtoObject*>(h.rt.globals())->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "GcProbe"), probe);

    const std::string churner =
        "Object subclass: #Churner.\n"
        "Churner >> churnUntil: stop\n"
        "  [ stop value ] whileFalse: [ Array new: 8 ].\n"
        "  ^ #churned\n";
    if (!hookOff) {
        const proto::ProtoObject* r = h.run(churner +
            "| stop f c0 |\n"
            "stop := Atom on: false.\n"
            "f := Churner new asActor churnUntil: stop.\n"
            "c0 := GcProbe gcCycles.\n"
            "[ GcProbe gcCycles > c0 ] whileFalse: [ ].\n"
            "stop value: true ifCurrent: false.\n"
            "f wait.\n");
        REQUIRE(r != nullptr);
        REQUIRE(r->asString(ctx) != nullptr);
    } else {
        const proto::ProtoObject* r = h.run(churner +
            "| stop f c0 n delta |\n"
            "stop := Atom on: false.\n"
            "f := Churner new asActor churnUntil: stop.\n"
            "[ GcProbe stopTheWorldRequested ] whileFalse: [ ].\n"
            "c0 := GcProbe gcCycles.\n"
            "n := 0.\n"
            "[ n < 3000000 ] whileTrue: [ n := n + 1 ].\n"
            "delta := GcProbe gcCycles - c0.\n"
            "stop value: true ifCurrent: false.\n"
            "f wait.\n"
            "delta.\n");
        REQUIRE(r != nullptr);
        // No collection completed while the loop ran without parking.
        REQUIRE(r->asLong(ctx) == 0);
    }
}
