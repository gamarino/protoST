#pragma once

// Track 1, slice 2 (EXC-a): the exception handler stack.
//
// A per-OS-thread stack of active `on:do:` handler entries, maintained
// SEPARATELY from the C++ call stack and from the engine's `frames_`. `signal`
// walks this stack — with the signalling C++ stack still intact — to find and
// run a handler; it does not use C++ throw/catch to LOCATE a handler.
//
// Why thread_local: an actor message and the foreground top-level each run on
// their own OS thread (workers + main). A handler pushed inside one actor's
// `on:do:` must never be visible to another actor draining concurrently.
// Each thread therefore owns an independent stack.

#include <cstddef>
#include <vector>
#include "protoCore.h"  // proto::proto_ulong

namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

// One active handler. Created by `on:do:` (push) and removed on every exit
// path (pop). While the handler block runs, the entries of its construct —
// and every inner one — are disabled so a `signal` inside the handler escapes
// to an OUTER handler.
//
// A construct with several guard/handler pairs (`on:do:on:do:`) pushes one
// entry per pair, the LAST pair first, so the first pair written is the
// innermost and is searched first, as the clauses of a try/catch are. Its
// entries share `constructId`, the id of its outermost entry (for a single
// `on:do:`, the entry's own id): while one of its handlers runs, and when one
// does `pass`, the search skips every pair of the construct, as a catch
// clause never catches what a sibling clause raised.
struct HandlerEntry {
    const proto::ProtoObject* guardClass   = nullptr; // Error / Warning / user
    const proto::ProtoObject* handlerBlock = nullptr; // a BlockClosure
    proto::proto_ulong             handlerId    = 0;       // unique, process-global
    proto::proto_ulong             constructId  = 0;       // the construct's outermost entry
    bool                      enabled      = true;
};

// Push a new handler entry; returns its unique handlerId. The id is drawn from
// a process-global atomic counter so ids are never reused, even across
// threads — that makes an UnwindToHandler.handlerId an unambiguous target.
//
// `constructId` is the id of an entry of the same construct pushed before
// this one (its outermost); 0 makes this entry its construct's outermost.
proto::proto_ulong handlerStackPush(const proto::ProtoObject* guardClass,
                               const proto::ProtoObject* handlerBlock,
                               proto::proto_ulong constructId = 0);

// Remove the entry with the given handlerId. Idempotent: a no-op if the entry
// is no longer present (so `on:do:` may call it on multiple exit paths).
void handlerStackPop(proto::proto_ulong handlerId);

// Find the newest *enabled* entry whose guardClass matches `exceptionInstance`
// (the instance is the guard class itself, or a prototype-chain descendant of
// it — protoCore `hasParent` / identity). Returns nullptr if none matches.
//
// `searchBelowId` supports `pass` (EXC-b): when non-zero, the search skips the
// entry with that id and every entry inner to it, resuming from the first
// entry OUTER to it — so a handler that did `pass` hands off to a strictly
// outer handler. When zero the search starts at the top of the stack (the
// ordinary `signal` entry point).
//
// The returned pointer is into the thread-local vector; it is valid only until
// the next push/pop on this thread. Callers (signal) read handlerId / block
// from it immediately.
//
// `catchAllGuard`, when set, is a guard that matches ANY value: the runtime
// passes the `Exception` class for a foreign exception (a module's error, a
// value raised by another language, a raised non-exception value), so
// `on: Exception do:` is the one handler form that catches every signalled
// value (LANGUAGE.md §8.7). It also counts as a member of an ExceptionSet.
const HandlerEntry* handlerStackFindMatch(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exceptionInstance,
                                          proto::proto_ulong searchBelowId = 0,
                                          const proto::ProtoObject* catchAllGuard = nullptr);

// The entry with the given handlerId, or nullptr when it is not on this
// thread's stack. Same lifetime rule as handlerStackFindMatch. Used by an
// `on:do:` that runs its handler after a NestingLimitUnwind (S23).
const HandlerEntry* handlerStackFindById(proto::proto_ulong handlerId);

// Disable the entry with id `targetHandlerId` and every entry inner to it
// (pushed after it), so a `signal` raised while the handler runs is caught by
// an OUTER handler — never the handler's own `on:do:` nor anything nested
// inside the protected block. Returns the ids of the entries this call
// actually flipped (entries already disabled are left untouched and omitted),
// to be passed verbatim to handlerStackRestore.
std::vector<proto::proto_ulong> handlerStackDisableFrom(proto::proto_ulong targetHandlerId);

// Re-enable exactly the entries listed by a matching handlerStackDisableFrom
// call. Entries that have since been popped are silently skipped.
// Disable every handler on this thread's stack (another actor's message is
// about to run here, and must not see them); handlerStackRestore undoes it.
std::vector<proto::proto_ulong> handlerStackDisableAll();
void handlerStackRestore(const std::vector<proto::proto_ulong>& disabledIds);

// Hide every handler block now running on this thread from the handler
// actions (`resume:`, `return:`, `retry`, `pass`, `outer` find their handler
// on the thread's active-signal stack, exception_prims.cpp): code run while
// they are hidden sees none of them, as it sees none of the disabled handler
// entries. Returns the previous floor, which activeSignalsRestore takes back.
// Strictly nested, like the code they bracket.
std::size_t activeSignalsHide();
void activeSignalsRestore(std::size_t previousFloor);

// A barrier between the code that runs inside it and the protoST code on the
// C++ stack below it: the handlers below are disabled and their running
// handler blocks hidden for the barrier's lifetime, and restored on every
// exit. Used where code that does not belong to the activation below runs on
// its thread: another actor's message while this one waits (waitHelping), a
// send from a foreign runtime's frame, a debugger evaluation.
class HandlerBarrier {
public:
    HandlerBarrier()
        : disabled_(handlerStackDisableAll()), activeFloor_(activeSignalsHide()) {}
    ~HandlerBarrier() {
        activeSignalsRestore(activeFloor_);
        handlerStackRestore(disabled_);
    }
    // True when there was a handler below to hide.
    bool hidOuterHandlers() const { return !disabled_.empty(); }
    HandlerBarrier(const HandlerBarrier&) = delete;
    HandlerBarrier& operator=(const HandlerBarrier&) = delete;

private:
    std::vector<proto::proto_ulong> disabled_;
    std::size_t activeFloor_;
};

} // namespace protoST
