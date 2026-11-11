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
// tests/conformance/09-collections/collect-deep-nesting-gc.st.
//
// The other cases cover the overflow chain's edges: exception unwinding
// through 600 pins, chain pins of two contexts interleaved on one thread with
// a reset in the middle, a pin made with a null object, and (POSIX) a chain
// pin released out of LIFO order, which must abort the process in every build.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#if !defined(_WIN32)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

// The three cases below were written by the K15 review (2026-11-11).

TEST_CASE("K15: exception unwinding through chain pins restores the chain", "[pin]") {
    protoST::STRuntime rt;
    proto::ProtoContext* ctx = rt.rootCtx();
    std::vector<const proto::ProtoObject*> objs;
    std::vector<protoST::TransientPin*> pins;
    REQUIRE_THROWS_AS(nestPins(ctx, 0, 600, objs, pins, [] { throw std::runtime_error("x"); }),
                      std::runtime_error);
    CHECK(protoST::g_scratchCursor == 0);
    CHECK(protoST::overflowPinsOnThisThread() == 0);
    CHECK(ctx->getAutomaticLocal(protoST::kOverflowHeadSlot) == PROTO_NONE);
    CHECK(ctx->getAutomaticLocal(protoST::kOverflowStageSlot) == PROTO_NONE);
}

TEST_CASE("K15: chain pins interleaved over two contexts on one thread", "[pin]") {
    protoST::STRuntime rt;
    proto::ProtoContext* a = rt.rootCtx();
    auto b = std::make_unique<proto::ProtoContext>(rt.space(), a);

    // Heap-allocated so that the test can hold many; destroyed LIFO below.
    std::vector<std::unique_ptr<protoST::TransientPin>> pins;
    std::vector<const proto::ProtoObject*> objA, objB;
    std::vector<protoST::TransientPin*> pinA;
    for (int i = 0; i < 254; ++i)
        pins.push_back(std::make_unique<protoST::TransientPin>(a, freshObject(a, i)));
    REQUIRE(protoST::g_scratchCursor == protoST::kScratchPinSlots);
    for (int i = 0; i < 200; ++i) {
        proto::ProtoContext* c = (i % 3 == 0) ? b.get() : a;
        const proto::ProtoObject* o = freshObject(c, 1000 + i);
        pins.push_back(std::make_unique<protoST::TransientPin>(c, o));
        (c == a ? objA : objB).push_back(o);
        if (c == a) pinA.push_back(pins.back().get());
    }
    auto check = [&] {
        auto ra = reachableFromSlots(a), rb = reachableFromSlots(b.get());
        std::size_t missA = 0, missB = 0;
        for (auto* o : objA) if (!ra.count(o)) ++missA;
        for (auto* o : objB) if (!rb.count(o)) ++missB;
        CHECK(missA == 0);
        CHECK(missB == 0);
    };
    check();
    // Reset a middle pin of `a` with pins of both contexts above it.
    const proto::ProtoObject* repl = freshObject(a, -1);
    const std::size_t mid = pinA.size() / 2;
    const proto::ProtoObject* oldMid = objA[mid];
    pinA[mid]->reset(repl);
    objA[mid] = repl;
    CHECK(pinA[mid]->pinned() == repl);
    for (std::size_t k = 0; k < pinA.size(); ++k) CHECK(pinA[k]->pinned() == objA[k]);
    check();
    CHECK(reachableFromSlots(a).count(oldMid) == 0);
    // Reset the innermost pin of `a` to nullptr and back.
    pinA.back()->reset(nullptr);
    CHECK(pinA.back()->pinned() == PROTO_NONE);
    pinA.back()->reset(objA.back());
    check();
    while (!pins.empty()) pins.pop_back();
    CHECK(protoST::g_scratchCursor == 0);
    CHECK(protoST::overflowPinsOnThisThread() == 0);
    CHECK(a->getAutomaticLocal(protoST::kOverflowHeadSlot) == PROTO_NONE);
    CHECK(b->getAutomaticLocal(protoST::kOverflowHeadSlot) == PROTO_NONE);
}

TEST_CASE("K15: a pin made with a null object claims a slot and roots after reset", "[pin]") {
    protoST::STRuntime rt;
    proto::ProtoContext* ctx = rt.rootCtx();
    protoST::TransientPin p(ctx, nullptr);
    CHECK(protoST::g_scratchCursor == 1);
    const proto::ProtoObject* o = freshObject(ctx, 5);
    p.reset(o);
    CHECK(reachableFromSlots(ctx).count(o) == 1);
}

#if !defined(_WIN32)
// A chain pin released while a pin created after it is live would restore a
// head that un-roots that later pin, so it must stop the process, in release
// builds too. The misuse runs in a forked child (only the forking thread
// exists there, and the release touches no lock before it aborts); the parent
// checks that the child died of SIGABRT with the message.
TEST_CASE("K15: an out-of-order release of a chain pin aborts in every build", "[pin]") {
    protoST::STRuntime rt;
    proto::ProtoContext* ctx = rt.rootCtx();
    std::vector<std::unique_ptr<protoST::TransientPin>> pins;
    for (int i = 0; i < 256; ++i)   // 254 region pins, then two chain pins
        pins.push_back(std::make_unique<protoST::TransientPin>(ctx, freshObject(ctx, i)));
    REQUIRE(protoST::overflowPinsOnThisThread() == 2);

    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        // Catch2's signal handler would report the abort as a failure of
        // this test from inside the child: let it kill the child instead.
        ::signal(SIGABRT, SIG_DFL);
        ::close(fds[0]);
        ::dup2(fds[1], 2);
        ::close(1);
        pins[254].reset();   // the lower chain pin, while pins[255] is live
        ::_exit(0);          // not reached
    }
    ::close(fds[1]);
    std::string err;
    char buf[512];
    for (ssize_t n; (n = ::read(fds[0], buf, sizeof buf)) > 0; ) err.append(buf, static_cast<std::size_t>(n));
    ::close(fds[0]);
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);

    INFO("child stderr: " << err);
    CHECK(WIFSIGNALED(status));
    CHECK((WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT));
    CHECK(err.find("non-LIFO release of an overflow-chain pin") != std::string::npos);

    while (!pins.empty()) pins.pop_back();   // in order, in the parent
    CHECK(protoST::overflowPinsOnThisThread() == 0);
}
#endif
