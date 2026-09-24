// Track S — reachability of the messages in a ProtoMPSCQueue mailbox.
//
// The mailbox is a protoCore ProtoMPSCQueue held under the actor's
// `__mailbox__` attribute, and a turn drains it one whole batch at a time
// (`takeAll`). That moves a batch out of the queue and into a ProtoList which,
// for the length of the turn, lives in a C++ local — and a C++ local is not
// traced. Two protoST-side protections keep those messages reachable:
//
//   1. `MailboxCursor::adopt` pins the batch (TransientPin) for as long as the
//      cursor walks it, so the messages a turn has not reached yet are held by
//      something the collector can see while the turn runs user code.
//
//   2. `MailboxCursor::spill` writes the unprocessed tail of a batch to the
//      actor's `__pending__` attribute when a turn ends early — a FutureYield
//      parks the actor mid-batch. An attribute is traced and survives the turn;
//      a C++ local does not. It also keeps FIFO order, because `__pending__` is
//      read before the queue on the next turn.
//
// The second test below fails if protection 2 is removed.
//
// The first protection is proved by `tests/cli/test_cli_actor_payload_gc.sh`,
// not here. When Track S was written it could not be proved at all: protoST
// reclaimed nothing, so "survives a collection" was not a falsifiable statement
// in this runtime — bug S15, closed on 2026-09-24 by giving the interpreter a
// garbage-collection safepoint at the loop back-edge and at engine entry (see
// `ExecutionEngine::gcSafepoint`). The CLI fixture now queues 5,000 payloads
// that only the mailbox references under a heap ceiling low enough that cycles
// run throughout, and each handler checks its own payload; removing the
// `TransientPin` in `MailboxCursor::adopt` segfaults it.
//
// The two tests here stay as they are: they cover ordering and delivery, which
// are cheap to check in-process and do not need a collection.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// A runtime plus the compiled modules it is still running.
//
// `kept` is declared BEFORE `rt` so that it is destroyed AFTER it. A queued
// message names its method by a raw `__bc_ptr__` into a BytecodeModule, and a
// worker can still be dispatching one while ~STRuntime joins the pool: a test
// that lets its module die first crashes in pushFrame, and the crash looks
// exactly like a garbage-collection failure. It is not one.
struct ActorHost {
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

} // namespace

TEST_CASE("Track S: a mailbox backlog survives repeated collection requests",
          "[actors][mailbox][gc]") {
    ActorHost h;

    // A second thread asks for a collection throughout, so the requests land
    // while the backlog sits in the queue AND while the sink is draining it.
    // It is a plain std::thread on purpose: it only calls triggerGC, which
    // raises a request flag, and never touches a ProtoContext.
    std::atomic<bool> stop{false};
    std::thread collector([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            h.rt.space()->triggerGC();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // The payloads are reachable ONLY through the messages, and the messages
    // only through the queue: the loop keeps no collection of the arrays it
    // builds and drops every Future it is handed.
    const proto::ProtoObject* r = h.run(
        "Object subclass: #Sink instanceVariableNames: 'sum count'. "
        "Sink >> initialize sum := 0. count := 0. ^ self. "
        "Sink >> take: payload "
        "  | s | s := 0. "
        "  payload do: [ :e | s := s + e ]. "
        "  1 to: 20 do: [ :i | Array new: 8 ]. "
        "  sum := sum + s. count := count + 1. ^ sum. "
        "Sink >> total ^ sum. "
        "Sink >> howMany ^ count. "
        "base := Sink new. base initialize. sink := base asActor. "
        "WorkerPool stopProcessing. "
        "1 to: 400 do: [ :i | sink take: (Array with: i with: i with: i) ]. "
        "WorkerPool startProcessing. "
        "got := (sink total) wait. "
        "howMany := (sink howMany) wait. "
        "(got = (3 * (400 * 401 / 2))) and: [ howMany = 400 ].");

    stop.store(true, std::memory_order_relaxed);
    collector.join();

    REQUIRE(r != nullptr);
    REQUIRE(r == PROTO_TRUE);
}

TEST_CASE("Track S: a turn that yields mid-batch keeps the rest of the batch",
          "[actors][mailbox][gc]") {
    // `takeAll` hands the turn every queued message at once, so a turn that
    // parks on a Future is holding the tail of a batch that is no longer in the
    // queue. That tail has to reach the actor's `__pending__` attribute.
    //
    // The assertion is on a log string, not a count, because the failure this
    // test exists to catch is an ordering failure as much as a loss: a tail
    // pushed back onto the queue instead of parked in `__pending__` would land
    // behind anything sent in the meantime, and a counting assertion would not
    // notice.
    ActorHost h;
    const proto::ProtoObject* r = h.run(
        "Object subclass: #Prod. "
        "Prod >> one ^ 1. "
        "Object subclass: #Rec instanceVariableNames: 'prod log'. "
        "Rec >> setProd: p prod := p. log := ''. ^ self. "
        "Rec >> slow: n "
        "  | v | v := (prod one) wait. "
        "  log := log , 'S' , n printString. ^ log. "
        "Rec >> fast: n "
        "  log := log , 'F' , n printString. ^ log. "
        "Rec >> log ^ log. "
        "prod := Prod new asActor. "
        "recBase := Rec new. "
        "rec := recBase asActor. "
        "(rec setProd: prod) wait. "
        // Fill the mailbox in one go, so the first turn takes all four messages
        // as a single batch and then yields on the first of them.
        "WorkerPool stopProcessing. "
        "rec slow: 1. rec fast: 2. rec fast: 3. rec fast: 4. "
        "WorkerPool startProcessing. "
        "(rec log) wait.");
    REQUIRE(r != nullptr);
    const proto::ProtoString* s = r->asString(h.rt.rootCtx());
    REQUIRE(s != nullptr);
    REQUIRE(s->toStdString(h.rt.rootCtx()) == "S1F2F3F4");
}
