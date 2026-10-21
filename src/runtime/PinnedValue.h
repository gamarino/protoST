#pragma once

// PinnedValue -- a ProtoObject* that stays a GC root while a protoST
// control-flow carrier (NonLocalReturn, UnwindToHandler, ResumeSignal) is in
// flight.
//
// Why. Those carriers are C++ exceptions that take a value (the `^` result,
// the handler's result, the `resume:` value) across C++ unwinding. Unwinding
// runs `ensure:` / `ifCurtailed:` blocks, which allocate and may reach a
// collection; by then no frame and no TransientPin holds the value any more
// (TransientPin slots are released LIFO as the frames unwind), so a raw
// pointer in the carrier would dangle.
//
// How. The pin is protoCore's own: the one that keeps a ProtoException's value
// alive in flight. A proto::ProtoException is held as a MEMBER, never as a
// base and never thrown: its constructor pins the value in the space's
// exception root set, copies share that pin (the C++ runtime copies an
// exception object when it is thrown or captured in a std::exception_ptr), and
// the last copy releases it. A null or tagged value is not pinned at all.
//
// Why a member and not a base class. A carrier is control flow private to
// protoST, not a language value: it targets one protoST frame (a home method,
// an on:do: activation, a signal loop). If it derived from ProtoException, the
// handler of any runtime on protoCore -- including protoST's own native
// boundary, which signals every ProtoException as a Smalltalk exception --
// would catch it with `catch (const proto::ProtoException&)`, look at the
// value (an arbitrary Smalltalk object), and either swallow a `^` / `return:`
// in an `except:`-style catch-all or rethrow it after running language-level
// handler selection on a value that was never raised. Kept out of that
// hierarchy, it passes through every language handler and reaches only the
// protoST frame that owns it.
//
// Lifetime: like a ProtoException, a carrier must not outlive the space its
// value lives in. Every carrier is caught inside the runtime (the frame it
// targets, or the stray-carrier handlers of runTopLevel and the actor drain).

#include "protoCore.h"

namespace protoST {

class PinnedValue {
public:
    PinnedValue(proto::ProtoContext* ctx, const proto::ProtoObject* value)
        : holder_(ctx, value ? value : PROTO_NONE) {}

    const proto::ProtoObject* get() const noexcept { return holder_.value(); }

private:
    // Used only for its shared, space-rooted pin; never thrown.
    proto::ProtoException holder_;
};

} // namespace protoST
