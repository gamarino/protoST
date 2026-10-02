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
// blocked, e.g. in Future>>wait, and reaches no safepoint) ends the process as
// the platform's default action would, so it can always be stopped: SIGINT's
// default action on POSIX, ExitProcess(STATUS_CONTROL_C_EXIT) on Windows.
//
// At the prompt, Ctrl-C cancels the line being typed (takePendingInterrupt).
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

// Takes a pending Ctrl-C: answers whether one was pending and clears it, and
// consumes the wake-up below. The REPL calls it while it waits for a line, so
// Ctrl-C at the prompt cancels the line being typed instead of staying pending
// (where a second one would end the process).
bool takePendingInterrupt();

// What a Ctrl-C signals besides setting the flag, so that a wait for input can
// end on it: the read end of a non-blocking self-pipe (POSIX) or a
// manual-reset event (Windows). Valid after armInterrupts.
#if defined(_WIN32)
void* interruptWakeEvent();
#else
int interruptWakeFd();
#endif

// Throws InterruptSignal when Ctrl-C is pending and `ctx` runs on the armed
// ProtoThread. A relaxed atomic load when nothing is pending.
void pollInterrupt(proto::ProtoContext* ctx);

} // namespace protoST
