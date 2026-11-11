// K15 — a TransientPin must root its object however many pins are live.
//
// A TransientPin keeps a ProtoObject* that lives in a C++ local alive across
// allocations by writing it into the engine context's automaticLocals, which
// the collector traces. Until 0.9.1 the pins lived only in a fixed scratch
// region of 256 slots; the 257th live pin hit `assert(false)` and, in a release
// build, returned without pinning anything. Nesting reaches that easily: every
// `collect:` keeps its accumulator pinned while its block runs, and a block
// that recurses into `collect:` 400 levels deep holds 400 such pins. The
// unpinned accumulators were freed by the next collection and the primitive
// then appended to a recycled cell (SIGSEGV in ProtoList::appendLast, or
// "stack smashing detected").
//
// The first case is deterministic and needs no collection: it holds 1000 live
// pins and checks that every pinned object is reachable from the context's
// traced slots. Before the fix a release build fails it (pins 257 on are
// stored nowhere: "744 of 1000 pinned objects unreachable") and a debug build
// aborts on the assert. The collection itself is exercised end to end by
// tests/cli/test_cli_deep_collect_gc.sh.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <functional>
#include <unordered_set>
#include <vector>

namespace {

constexpr int kDepth = 1000;   // far past the 256-slot scratch region

// Every object reachable from ctx's automaticLocals through lists: a slot's
// value, and transitively the elements of every list met on the way.
std::unordered_set<const proto::ProtoObject*> reachableFromSlots(proto::ProtoContext* ctx) {
    std::unordered_set<const proto::ProtoObject*> seen;
    std::vector<const proto::ProtoObject*> work;
    for (unsigned int i = 0; i < ctx->getAutomaticLocalsCount(); ++i)
        work.push_back(ctx->getAutomaticLocal(i));
    while (!work.empty()) {
        const proto::ProtoObject* o = work.back();
        work.pop_back();
        if (!o || o == PROTO_NONE || o->isInteger(ctx) || !seen.insert(o).second) continue;
        const proto::ProtoList* l = o->asList(ctx);
        if (!l) continue;
        const int n = static_cast<int>(l->getSize(ctx));
        for (int k = 0; k < n; ++k) work.push_back(l->getAt(ctx, k));
    }
    return seen;
}

// A fresh one-element list [i]: a heap cell nothing else references.
const proto::ProtoObject* freshObject(proto::ProtoContext* ctx, int i) {
    return reinterpret_cast<const proto::ProtoObject*>(
        ctx->newList()->appendLast(ctx, ctx->fromLong(i)));
}

// Holds `depth` nested pins, one per level, each on a fresh object, and runs
// `atBottom` with all of them live.
void nestPins(proto::ProtoContext* ctx, int level, int depth,
              std::vector<const proto::ProtoObject*>& objs,
              std::vector<protoST::TransientPin*>& pins,
              const std::function<void()>& atBottom) {
    if (level == depth) { atBottom(); return; }
    const proto::ProtoObject* o = freshObject(ctx, level);
    protoST::TransientPin pin(ctx, o);
    objs.push_back(o);
    pins.push_back(&pin);
    nestPins(ctx, level + 1, depth, objs, pins, atBottom);
    pins.pop_back();
}

} // namespace

TEST_CASE("K15: 1000 live transient pins all root their objects", "[gc][pin]") {
    protoST::STRuntime rt;
    proto::ProtoContext* ctx = rt.rootCtx();

    std::vector<const proto::ProtoObject*> objs;
    std::vector<protoST::TransientPin*> pins;
    std::size_t unreachable = 0;
    std::size_t unreachableAfterReset = 0;
    nestPins(ctx, 0, kDepth, objs, pins, [&] {
        const auto live = reachableFromSlots(ctx);
        for (const auto* o : objs)
            if (!live.count(o)) ++unreachable;

        // Re-point a pin deep in the middle, with hundreds of pins above it:
        // the new object must be rooted and every other pin must keep its own.
        const proto::ProtoObject* replacement = freshObject(ctx, -7);
        pins[600]->reset(replacement);
        objs[600] = replacement;
        const auto live2 = reachableFromSlots(ctx);
        for (const auto* o : objs)
            if (!live2.count(o)) ++unreachableAfterReset;
    });

    INFO(unreachable << " of " << kDepth << " pinned objects unreachable from the traced slots");
    CHECK(unreachable == 0);
    CHECK(unreachableAfterReset == 0);

    // Every pin released: no slot still holds one of the objects.
    const auto after = reachableFromSlots(ctx);
    std::size_t retained = 0;
    for (const auto* o : objs)
        if (after.count(o)) ++retained;
    CHECK(retained == 0);
    CHECK(protoST::g_scratchCursor == 0);
}
