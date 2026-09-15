// D27 regression tests for the scheduler's lock-free ReadyStack.
//
// The stack is a Treiber stack. A pop reads the head node and its successor,
// then compare-and-swaps the head from that node to the successor. Two
// hazards follow when popped nodes are freed and reallocated:
//   * use-after-free: the successor is read from a node another thread may
//     already have popped and freed;
//   * ABA: between the read and the compare-and-swap, other threads may pop
//     the node, pop its successor and push a new node that reuses the first
//     node's memory. The compare-and-swap then succeeds and installs a
//     successor that is no longer in the stack.
#include <catch2/catch_all.hpp>

#include "runtime/ReadyStack.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using protoST::ReadyStack;

namespace {

// Runs `action` once, inside the next pop, after that pop has read the head
// and its successor and right before it attempts the compare-and-swap.
struct InterleaveHook {
    template <class Stack>
    static void beforePopCommit(Stack& s) {
        if (!armed) return;
        armed = false;            // the nested pops below must not re-enter
        action(static_cast<void*>(&s));
    }
    static inline bool armed = false;
    static inline void (*action)(void*) = nullptr;
};

using HookedStack = ReadyStack<std::uintptr_t, InterleaveHook>;

std::vector<std::uintptr_t> g_innerPops;
std::unique_ptr<char[]>     g_unrelated;

} // namespace

// Deterministic single-threaded reproduction of the ABA interleaving. The
// "other thread" runs inside the hook, so the exact schedule is fixed:
//   outer pop reads head = node(1), successor = node(2)
//   interleaved:  pop -> 1, pop -> 2, an unrelated allocation of a node's
//                 size, push 4
//   outer pop compare-and-swaps.
// With nodes returned to malloc, the unrelated allocation takes node(2)'s
// memory and the pushed node takes node(1)'s address, so the outer CAS
// succeeds and installs the freed node(2) as the new head. A correct stack
// rejects the stale CAS, retries and pops 4, leaving exactly {3}.
TEST_CASE("ReadyStack: a pop that loses a pop/pop/push race stays consistent",
          "[scheduler][readystack]") {
    HookedStack s;
    s.push(3);
    s.push(2);
    s.push(1);                    // head: 1 -> 2 -> 3

    g_innerPops.clear();
    // Reserve up front: the hook must not allocate anything but the one
    // unrelated block, or it would disturb which freed memory gets reused.
    g_innerPops.reserve(8);
    InterleaveHook::action = [](void* p) {
        auto& st = *static_cast<HookedStack*>(p);
        g_innerPops.push_back(st.pop());   // 1
        g_innerPops.push_back(st.pop());   // 2
        // Any allocation on this thread may reuse a freed node's memory.
        g_unrelated.reset(new char[2 * sizeof(void*)]());
        st.push(4);                         // head: 4 -> 3
    };
    InterleaveHook::armed = true;
    const std::uintptr_t outer = s.pop();

    std::vector<std::uintptr_t> popped = g_innerPops;
    popped.push_back(outer);
    std::sort(popped.begin(), popped.end());
    REQUIRE(popped == std::vector<std::uintptr_t>{1, 2, 4});

    std::vector<std::uintptr_t> rest;
    for (int guard = 0; guard < 16; ++guard) {
        const std::uintptr_t v = s.pop();
        if (v == 0) break;
        rest.push_back(v);
    }
    REQUIRE(rest == std::vector<std::uintptr_t>{3});
    REQUIRE(s.approxSize() == 0);
}

// Every value pushed is popped exactly once under heavy contention: 8 threads
// each push their own distinct tokens and pop twice after every second push,
// so the stack is usually several nodes deep (the ABA precondition) and nodes
// are recycled constantly. Remaining values are drained at the end.
TEST_CASE("ReadyStack: concurrent push/pop conserves every element",
          "[scheduler][readystack][concurrency]") {
    constexpr int            kThreads   = 8;
    constexpr std::uintptr_t kPerThread = 250000;
    const size_t kTokens = static_cast<size_t>(kThreads) * kPerThread;

    ReadyStack<std::uintptr_t> s;
    std::vector<std::atomic<uint8_t>> seen(kTokens + 1);
    std::atomic<long> invalid{0};
    std::atomic<bool> go{false};

    auto record = [&](std::uintptr_t v) {
        if (v == 0 || v > kTokens) {
            invalid.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        seen[v].fetch_add(1, std::memory_order_relaxed);
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();
            const std::uintptr_t base = static_cast<std::uintptr_t>(t) * kPerThread;
            for (std::uintptr_t i = 1; i <= kPerThread; ++i) {
                s.push(base + i);
                if (i % 2 == 0) {
                    for (int k = 0; k < 2; ++k) {
                        if (std::uintptr_t v = s.pop()) record(v);
                    }
                }
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();
    for (std::uintptr_t v = s.pop(); v != 0; v = s.pop()) record(v);

    size_t missing = 0, duplicated = 0;
    for (size_t v = 1; v <= kTokens; ++v) {
        const uint8_t c = seen[v].load(std::memory_order_relaxed);
        if (c == 0) ++missing;
        else if (c > 1) ++duplicated;
    }
    REQUIRE(invalid.load() == 0);
    REQUIRE(missing == 0);
    REQUIRE(duplicated == 0);
    REQUIRE(s.approxSize() == 0);
}
