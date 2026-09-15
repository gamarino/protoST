#pragma once
// ReadyStack: the scheduler's lock-free LIFO stack of ready actors (D27).
//
// A Treiber stack whose nodes live in a type-stable pool and whose head is
// tagged with a generation counter.
//
// Why. The original version allocated a node per push and deleted it per pop.
// A pop reads the head node's successor and then compare-and-swaps the head
// from that node to the successor. With nodes returned to malloc that is:
//   * a use-after-free read: another thread may pop and delete the head node
//     between this thread's load and its read of `next`;
//   * an ABA hazard: other threads may pop the head node, pop its successor
//     and push a new node that malloc places at the first node's address.
//     glibc's tcache is a per-thread LIFO that hands a freed chunk straight
//     back to the next allocation of its size, so this is not rare. The stale
//     compare-and-swap then succeeds and installs a node that is no longer in
//     the stack: entries are lost, a freed block becomes reachable from the
//     head, and a later pop frees it a second time.
//
// How.
//   * Nodes are never freed while the stack lives. They are carved out of
//     chunks that are only ever added (each chunk doubles the capacity); a
//     popped node goes onto a lock-free free list and is reused by a later
//     push. Reading a node through a stale index therefore always reads valid
//     memory; the value read may be stale, and the compare-and-swap below
//     rejects it.
//   * A node is named by a 32-bit index. Both heads (the stack and its free
//     list) are 64-bit words holding (generation << 32) | index, and every
//     successful compare-and-swap installs generation + 1. A thread's
//     compare-and-swap therefore fails if any other update happened since it
//     read the head, even when the same index is back on top. A false match
//     would need exactly 2^32 successful updates of that head between one
//     thread's load and its compare-and-swap.
//   * `next` links are atomics: a thread holding a stale index may read a
//     link while the node's current owner rewrites it. The stale value is
//     then rejected by the compare-and-swap.
//
// Cost. push = one free-list pop + one head compare-and-swap; pop = one head
// compare-and-swap + one free-list push. No malloc/free in steady state; a
// chunk is allocated only when the number of simultaneously queued entries
// reaches a new maximum. Memory: 16 bytes per node for pointer values,
// retained at that high-water mark until the stack is destroyed.
//
// `Hook` is a zero-cost test seam: Hook::beforePopCommit(stack) runs inside
// pop() between reading the successor and the compare-and-swap, so tests can
// force an interleaving deterministically. Production uses ReadyStackNoHook.
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>

namespace protoST {

struct ReadyStackNoHook {
    template <class Stack> static void beforePopCommit(Stack&) {}
};

template <class V, class Hook = ReadyStackNoHook>
class ReadyStack {
public:
    ReadyStack() = default;
    ReadyStack(const ReadyStack&) = delete;
    ReadyStack& operator=(const ReadyStack&) = delete;

    // Destruction requires that no other thread uses the stack any more.
    // Values are not owned.
    ~ReadyStack() {
        for (auto& chunk : chunks_)
            delete[] chunk.load(std::memory_order_acquire);
    }

    void push(V value) {
        const uint32_t idx = acquireNode();
        node(idx).value = value;       // exclusively owned until published
        linkFront(head_, idx);
        size_.fetch_add(1, std::memory_order_relaxed);
    }

    // Returns V{} when the stack is empty.
    V pop() {
        uint32_t idx;
        if (!unlinkFront(head_, idx, /*runHook=*/true)) return V{};
        const V value = node(idx).value;   // exclusively owned after unlinking
        linkFront(free_, idx);
        size_.fetch_sub(1, std::memory_order_relaxed);
        return value;
    }

    size_t approxSize() const {
        // Diagnostic only; not synchronised with concurrent push/pop.
        long long s = size_.load(std::memory_order_relaxed);
        return s > 0 ? static_cast<size_t>(s) : 0;
    }

private:
    struct Node {
        V                     value{};
        std::atomic<uint32_t> next{kNil};
    };

    static constexpr uint32_t kNil = UINT32_MAX;
    // Chunk k holds 2^(kFirstChunkBits + k) nodes; kMaxChunks chunks give
    // 2^32 - 2^kFirstChunkBits indices, all below kNil.
    static constexpr unsigned kFirstChunkBits = 8;
    static constexpr unsigned kMaxChunks      = 32 - kFirstChunkBits;
    static constexpr uint64_t kCapacity =
        (uint64_t{1} << 32) - (uint64_t{1} << kFirstChunkBits);

    static uint64_t pack(uint64_t generation, uint32_t idx) {
        return (generation << 32) | idx;
    }
    static uint32_t indexOf(uint64_t word)      { return static_cast<uint32_t>(word); }
    static uint64_t generationOf(uint64_t word) { return word >> 32; }

    Node& node(uint32_t idx) const {
        const uint64_t v = uint64_t{idx} + (uint64_t{1} << kFirstChunkBits);
        const unsigned k =
            static_cast<unsigned>(std::bit_width(v)) - 1 - kFirstChunkBits;
        const size_t offset =
            static_cast<size_t>(v - (uint64_t{1} << (kFirstChunkBits + k)));
        // Every index a thread can observe was published after its chunk was
        // installed, and chunks are never removed.
        return chunks_[k].load(std::memory_order_acquire)[offset];
    }

    void linkFront(std::atomic<uint64_t>& head, uint32_t idx) {
        Node& n = node(idx);
        uint64_t old = head.load(std::memory_order_acquire);
        for (;;) {
            n.next.store(indexOf(old), std::memory_order_release);
            if (head.compare_exchange_weak(old, pack(generationOf(old) + 1, idx),
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire))
                return;
        }
    }

    bool unlinkFront(std::atomic<uint64_t>& head, uint32_t& out, bool runHook) {
        uint64_t old = head.load(std::memory_order_acquire);
        for (;;) {
            const uint32_t idx = indexOf(old);
            if (idx == kNil) return false;
            // May be stale if another thread already took the node; the
            // generation makes the compare-and-swap below fail in that case.
            const uint32_t next = node(idx).next.load(std::memory_order_acquire);
            if (runHook) Hook::beforePopCommit(*this);
            if (head.compare_exchange_weak(old, pack(generationOf(old) + 1, next),
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
                out = idx;
                return true;
            }
        }
    }

    uint32_t acquireNode() {
        uint32_t idx;
        if (unlinkFront(free_, idx, /*runHook=*/false)) return idx;
        // Free list empty: take a never-used index, installing its chunk if
        // this is the chunk's first index.
        const uint64_t fresh = nextFresh_.fetch_add(1, std::memory_order_acq_rel);
        if (fresh >= kCapacity) throw std::bad_alloc();
        idx = static_cast<uint32_t>(fresh);
        const uint64_t v = fresh + (uint64_t{1} << kFirstChunkBits);
        const unsigned k =
            static_cast<unsigned>(std::bit_width(v)) - 1 - kFirstChunkBits;
        if (!chunks_[k].load(std::memory_order_acquire)) {
            Node* chunk = new Node[size_t{1} << (kFirstChunkBits + k)];
            Node* expected = nullptr;
            if (!chunks_[k].compare_exchange_strong(expected, chunk,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
                delete[] chunk;          // another thread installed it first
        }
        return idx;
    }

    std::atomic<uint64_t> head_{uint64_t{kNil}};
    std::atomic<uint64_t> free_{uint64_t{kNil}};
    std::atomic<uint64_t> nextFresh_{0};
    std::atomic<long long> size_{0};
    std::atomic<Node*>    chunks_[kMaxChunks] = {};
};

} // namespace protoST
