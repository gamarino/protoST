#pragma once
// Ctrl-C handling for the interactive REPL.
//
// SIGINT only sets a flag. The engine polls it at its safepoints (loop
// back-edges and engine entry), on the ProtoThread that armed interrupts --
// the REPL's -- and raises InterruptSignal there. InterruptSignal is a control
// exception like DebuggerHalt: the native-exception bridge re-throws it
// untouched, so no `on: Error do:` handler can swallow it, and the REPL
// reports it and returns to its prompt.
//
// A second Ctrl-C that arrives before the first one was taken (the program is
// blocked, e.g. in Future>>wait, and reaches no safepoint) restores the
// default action, so the process can always be stopped.
//
// Script and -e modes never arm interrupts: SIGINT keeps its default action
// and the process ends with status 130.

#include <stdexcept>

namespace proto { class ProtoContext; }

namespace protoST {

struct InterruptSignal : std::runtime_error {
    InterruptSignal() : std::runtime_error("Interrupted") {}
};

// Installs the SIGINT handler and records the ProtoThread that owns `ctx`
// (ctx->thread; ProtoThread::getCurrentThread is declared by protoCore 2.5.0
// but not defined).
void armInterrupts(proto::ProtoContext* ctx);

// Discards a Ctrl-C pressed while no evaluation was running.
void clearPendingInterrupt();

// Throws InterruptSignal when Ctrl-C is pending and `ctx` runs on the armed
// ProtoThread. A relaxed atomic load when nothing is pending.
void pollInterrupt(proto::ProtoContext* ctx);

} // namespace protoST
