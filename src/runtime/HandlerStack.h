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

namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

// One active handler. Created by `on:do:` (push) and removed on every exit
// path (pop). While the handler block runs, the entry — and every inner one —
// is disabled so a `signal` inside the handler escapes to an OUTER handler.
struct HandlerEntry {
    const proto::ProtoObject* guardClass   = nullptr; // Error / Warning / user
    const proto::ProtoObject* handlerBlock = nullptr; // a BlockClosure
    unsigned long             handlerId    = 0;       // unique, process-global
    bool                      enabled      = true;
};

// Push a new handler entry; returns its unique handlerId. The id is drawn from
// a process-global atomic counter so ids are never reused, even across
// threads — that makes an UnwindToHandler.handlerId an unambiguous target.
unsigned long handlerStackPush(const proto::ProtoObject* guardClass,
                               const proto::ProtoObject* handlerBlock);

// Remove the entry with the given handlerId. Idempotent: a no-op if the entry
// is no longer present (so `on:do:` may call it on multiple exit paths).
void handlerStackPop(unsigned long handlerId);

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
const HandlerEntry* handlerStackFindMatch(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exceptionInstance,
                                          unsigned long searchBelowId = 0);

// Disable the entry with id `targetHandlerId` and every entry inner to it
// (pushed after it), so a `signal` raised while the handler runs is caught by
// an OUTER handler — never the handler's own `on:do:` nor anything nested
// inside the protected block. Returns the ids of the entries this call
// actually flipped (entries already disabled are left untouched and omitted),
// to be passed verbatim to handlerStackRestore.
std::vector<unsigned long> handlerStackDisableFrom(unsigned long targetHandlerId);

// Re-enable exactly the entries listed by a matching handlerStackDisableFrom
// call. Entries that have since been popped are silently skipped.
void handlerStackRestore(const std::vector<unsigned long>& disabledIds);

} // namespace protoST
