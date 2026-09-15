#include <catch2/catch_all.hpp>
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"
#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using protoST::BytecodeModule;
using protoST::Op;

TEST_CASE("BytecodeModule round-trips emit and decode", "[bytecode]") {
    BytecodeModule m;
    auto idx = m.addInteger(42);
    REQUIRE(idx == 0);
    m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
    m.emit(Op::RETURN_TOP, 0);

    REQUIRE(m.bytes().size() == 4);
    REQUIRE(static_cast<Op>(m.bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(m.bytes()[1] == 0);
    REQUIRE(static_cast<Op>(m.bytes()[2]) == Op::RETURN_TOP);

    REQUIRE(m.constInteger(0) == 42);
}

TEST_CASE("BytecodeModule supports symbol interning by string", "[bytecode]") {
    BytecodeModule m;
    auto a = m.internSymbol("value");
    auto b = m.internSymbol("value");
    auto c = m.internSymbol("at:put:");
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(m.constSymbol(a) == "value");
    REQUIRE(m.constSymbol(c) == "at:put:");
}

// D25 regression. constSym / ivSymbol fill per-module caches lazily. Several
// worker threads may execute a module for the first time at the same moment,
// so the first-use path must be safe under concurrency: no thread may observe
// a freed buffer, a null entry or a different symbol than its peers.
//
// Each round builds a fresh module (so every round exercises the first-use
// path again), releases kThreads threads on it at once, and has every thread
// resolve every constant in its own shuffled order. Symbol names are longer
// than six bytes so createSymbol takes the SymbolTable path (shard mutex),
// which widens the window between the first-use check and the cache write.
//
// The threads are plain std::threads sharing the root context. That is sound
// here only because interning a symbol never allocates from the context:
// protoCore builds interned strings with a null context (perpetual cells) and
// guards the table with its shard mutexes.
TEST_CASE("BytecodeModule symbol caches are safe under concurrent first use",
          "[bytecode][concurrency]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    constexpr int kThreads = 8;
    constexpr int kConsts  = 64;
    constexpr int kRounds  = 1500;

    std::vector<std::string> names;
    std::vector<const proto::ProtoString*> expectedSym, expectedIv;
    for (int i = 0; i < kConsts; ++i) {
        names.push_back("concurrentFirstUse" + std::to_string(i));
        expectedSym.push_back(
            proto::ProtoString::createSymbol(ctx, names.back().c_str()));
        expectedIv.push_back(proto::ProtoString::createSymbol(
            ctx, ("_iv_" + names.back()).c_str()));
    }

    std::atomic<BytecodeModule*> current{nullptr};
    std::atomic<int> phase{0};          // round number; -1 stops the workers
    std::atomic<int> finished{0};
    std::atomic<int> mismatches{0};

    auto worker = [&](int tid) {
        std::vector<int> order(kConsts);
        for (int i = 0; i < kConsts; ++i) order[i] = i;
        std::mt19937 rng(static_cast<unsigned>(tid) * 7919u + 1u);
        int seen = 0;
        for (;;) {
            int p;
            while ((p = phase.load(std::memory_order_acquire)) == seen)
                std::this_thread::yield();
            if (p < 0) return;
            seen = p;
            BytecodeModule* m = current.load(std::memory_order_acquire);
            std::shuffle(order.begin(), order.end(), rng);
            for (int idx : order) {
                bool ivFirst = (idx + tid) % 2 == 0;
                const proto::ProtoString* iv = nullptr;
                if (ivFirst) iv = m->ivSymbol(ctx, idx);
                const proto::ProtoString* sym = m->constSym(ctx, idx);
                if (!ivFirst) iv = m->ivSymbol(ctx, idx);
                if (sym != expectedSym[idx] || iv != expectedIv[idx])
                    mismatches.fetch_add(1, std::memory_order_relaxed);
            }
            finished.fetch_add(1, std::memory_order_release);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) threads.emplace_back(worker, t);

    std::vector<std::unique_ptr<BytecodeModule>> modules;
    for (int round = 1; round <= kRounds; ++round) {
        auto m = std::make_unique<BytecodeModule>();
        for (int i = 0; i < kConsts; ++i) m->internSymbol(names[i]);
        current.store(m.get(), std::memory_order_release);
        phase.store(round, std::memory_order_release);
        while (finished.load(std::memory_order_acquire) < round * kThreads)
            std::this_thread::yield();
        // Keep every module alive until the workers are joined: a worker
        // never touches a module after reporting, but destroying it here
        // would also free a (pre-fix) buffer a racing writer still targets,
        // hiding the corruption from the checks below.
        modules.push_back(std::move(m));
    }
    phase.store(-1, std::memory_order_release);
    for (auto& t : threads) t.join();

    REQUIRE(mismatches.load() == 0);
}

// D25 regression. A SEND_CALL descriptor is parsed off to the side and
// published once; every thread that races to publish must get the same,
// complete descriptor back, and a module that gains constants after its cache
// table was published must keep earlier results valid.
TEST_CASE("BytecodeModule call descriptors publish once under concurrency",
          "[bytecode][concurrency]") {
    constexpr int kThreads = 8;
    constexpr int kRounds  = 2000;

    for (int round = 0; round < kRounds; ++round) {
        BytecodeModule m;
        size_t idx = m.internSymbol("callForm#2#key");
        std::atomic<bool> go{false};
        std::vector<const BytecodeModule::CallDescriptor*> got(kThreads, nullptr);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                while (!go.load(std::memory_order_acquire))
                    std::this_thread::yield();
                const auto* d = m.callDescriptor(idx);
                if (!d) {
                    auto fresh = std::make_unique<BytecodeModule::CallDescriptor>();
                    fresh->nPos = 2;
                    fresh->mangled = "callForm#2#key";
                    d = m.publishCallDescriptor(idx, std::move(fresh));
                }
                got[t] = d;
            });
        }
        go.store(true, std::memory_order_release);
        for (auto& th : threads) th.join();

        REQUIRE(got[0] != nullptr);
        for (int t = 1; t < kThreads; ++t) REQUIRE(got[t] == got[0]);
        REQUIRE(m.callDescriptor(idx) == got[0]);
        REQUIRE(got[0]->nPos == 2);
        REQUIRE(got[0]->mangled == "callForm#2#key");
    }

    // Growth after first use: a hand-built module that gains a constant keeps
    // the descriptor it already published reachable and valid.
    BytecodeModule m;
    size_t first = m.internSymbol("first#0");
    auto d0 = std::make_unique<BytecodeModule::CallDescriptor>();
    d0->mangled = "first#0";
    const auto* published = m.publishCallDescriptor(first, std::move(d0));
    size_t second = m.internSymbol("second#1");
    auto d1 = std::make_unique<BytecodeModule::CallDescriptor>();
    d1->mangled = "second#1";
    const auto* published1 = m.publishCallDescriptor(second, std::move(d1));
    REQUIRE(published->mangled == "first#0");
    REQUIRE(published1->mangled == "second#1");
    REQUIRE(m.callDescriptor(second) == published1);
}
