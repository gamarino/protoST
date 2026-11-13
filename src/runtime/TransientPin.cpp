// TransientPin.cpp — K15: the overflow chain of TransientPin (see the header).
//
// A pin that finds the scratch region full is a CHAIN PIN. Its object is held
// by one ProtoList cell `[object, previous head]` whose head sits in the
// context's slot kOverflowHeadSlot, and its bookkeeping is an entry on this
// thread's chain-pin stack, t_chain. A chain pin's slot_ is kScratchSlots plus
// its index there. The entry records the pin's context and the head its cell
// replaced (the cell's second element), so a release restores that head
// without allocating.
//
// Several contexts can have chain pins on one thread (nested engines on a
// child context, an EntryContext); each context has its own chain, and the
// walks below skip entries of other contexts.

#include "TransientPin.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace protoST {

namespace {

struct ChainEntry {
    proto::ProtoContext*      ctx;
    const proto::ProtoObject* prev;      // the head this pin's cell replaced
    const proto::ProtoObject* pending;   // reset's new prev, before it commits
};

thread_local std::vector<ChainEntry> t_chain;

[[noreturn]] void fatal(const char* what) {
    std::fprintf(stderr, "protoST: fatal: TransientPin: %s\n", what);
    std::fflush(stderr);
    std::abort();
}

void store(proto::ProtoContext* ctx, unsigned int slot, const proto::ProtoObject* obj) {
    if (!ctx->setAutomaticLocal(slot, obj))
        fatal("an overflow-chain slot is out of range of the context's automaticLocals");
}

// One chain cell, [obj, below]. obj is staged in a traced slot across the
// allocation; `below` is already traced (the current head, or a cell built
// by the same reset, which is a young cell of ctx: protoCore's allocation
// parks but never submits a young chain).
const proto::ProtoObject* chainCell(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                    const proto::ProtoObject* below) {
    store(ctx, kOverflowStageSlot, obj);
    const proto::ProtoObject* items[2] = { obj, below };
    const proto::ProtoList* cell = ctx->newList(2, items);
    store(ctx, kOverflowStageSlot, PROTO_NONE);
    return reinterpret_cast<const proto::ProtoObject*>(cell);
}

const proto::ProtoObject* objectOf(proto::ProtoContext* ctx, const proto::ProtoObject* cell) {
    return reinterpret_cast<const proto::ProtoList*>(cell)->getAt(ctx, 0);
}

// The index on t_chain of the chain pin whose slot_ is `slot`, after checking
// that it names a live entry of `ctx`.
std::size_t entryOf(proto::ProtoContext* ctx, unsigned int slot) {
    const std::size_t i = slot - kScratchSlots;
    if (i >= t_chain.size() || t_chain[i].ctx != ctx)
        fatal("a chain pin names no live entry of its context "
              "(a pin used after its release, or on another thread)");
    return i;
}

}  // namespace

std::size_t overflowPinsOnThisThread() { return t_chain.size(); }

unsigned int TransientPin::pushChain(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    t_chain.reserve(t_chain.size() + 1);   // may throw: nothing changed yet
    const proto::ProtoObject* head = ctx->getAutomaticLocal(kOverflowHeadSlot);
    const proto::ProtoObject* cell = chainCell(ctx, obj, head);
    t_chain.push_back(ChainEntry{ ctx, head, nullptr });
    store(ctx, kOverflowHeadSlot, cell);
    return kScratchSlots + static_cast<unsigned int>(t_chain.size() - 1);
}

void TransientPin::popChain(proto::ProtoContext* ctx, unsigned int slot) {
    // A chain pin released before one created after it would restore a head
    // that un-roots every pin above it, silently. Never continue past that.
    if (slot - kScratchSlots + 1 != t_chain.size() || t_chain.back().ctx != ctx)
        fatal("non-LIFO release of an overflow-chain pin "
              "(a pin released while a pin created after it is still live)");
    store(ctx, kOverflowHeadSlot, t_chain.back().prev);
    t_chain.pop_back();
}

void TransientPin::resetChain(proto::ProtoContext* ctx, unsigned int slot,
                              const proto::ProtoObject* obj) {
    const std::size_t s0 = entryOf(ctx, slot);
    // A chain cell is immutable: this pin gets a new cell, and so does every
    // pin of ctx above it, each linked to the new one below. One upward pass:
    // the old cell of a pin is the prev of the next pin of ctx above it (the
    // head for the topmost), so each pin's cell is rebuilt when the pass
    // reaches the pin above it. Every cell is built before any entry's prev
    // or the head changes, so an allocation that throws leaves the chain as
    // it was; until the head is published the old chain roots every object.
    const proto::ProtoObject* built = chainCell(ctx, obj, t_chain[s0].prev);
    std::size_t cur = s0;
    for (std::size_t m = s0 + 1; m < t_chain.size(); ++m) {
        if (t_chain[m].ctx != ctx) continue;
        if (cur != s0)
            built = chainCell(ctx, objectOf(ctx, t_chain[m].prev), t_chain[cur].pending);
        t_chain[m].pending = built;
        cur = m;
    }
    if (cur != s0)
        built = chainCell(ctx, objectOf(ctx, ctx->getAutomaticLocal(kOverflowHeadSlot)),
                          t_chain[cur].pending);
    for (std::size_t m = s0 + 1; m < t_chain.size(); ++m)
        if (t_chain[m].ctx == ctx) t_chain[m].prev = t_chain[m].pending;
    store(ctx, kOverflowHeadSlot, built);
}

const proto::ProtoObject* TransientPin::chainPinned(proto::ProtoContext* ctx, unsigned int slot) {
    const std::size_t i = entryOf(ctx, slot);
    const proto::ProtoObject* cell = ctx->getAutomaticLocal(kOverflowHeadSlot);
    for (std::size_t m = t_chain.size(); m-- > i + 1; )
        if (t_chain[m].ctx == ctx) cell = t_chain[m].prev;
    return objectOf(ctx, cell);
}

}  // namespace protoST
