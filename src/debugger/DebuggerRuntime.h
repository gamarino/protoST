#pragma once
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "BreakpointTable.h"
namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;
class BytecodeModule;

struct DebugFrame {
    const BytecodeModule*                  module = nullptr;
    size_t                                 pc = 0;
    // The source line this frame is at: the line of the instruction at `pc`
    // for the frame that stopped before an instruction, the line of the send
    // in progress for a caller frame (whose pc is already past it). 0 when
    // unknown.
    int                                    line = 0;
    int                                    frameDepth = 0;
    const proto::ProtoObject*              self = nullptr;
    // The activation's captured-variable dict (its temporaries that a block
    // shares live there, not in their slots); nullptr or nil when none.
    const proto::ProtoObject*              captured = nullptr;
    std::vector<const proto::ProtoObject*> stack;
    std::vector<const proto::ProtoObject*> locals;

    // F8-4: the full call stack of the stopped thread at the stop point,
    // oldest frame first and the current (innermost) frame last, across every
    // nested engine of the thread (a block run by a primitive runs in an engine
    // of its own). Empty when no stack could be snapshotted; the single-frame
    // fields above are then the only data available.
    std::vector<DebugFrame>                callStack;
};

class DebuggerHalt : public std::runtime_error {
public:
    explicit DebuggerHalt(std::string reason)
        : std::runtime_error("Halt: " + reason), reason_(std::move(reason)) {}
    const std::string& reason() const { return reason_; }
private:
    std::string reason_;
};

class DebuggerFrontend;
struct DebugThreadState;

// A variable of a stopped frame, as the debugger shows it.
struct FrameVariable {
    std::string               name;
    const proto::ProtoObject* value;
};

// What a stop reports besides its frame. `threadId` is the stopped thread's
// debugger id; `threadName` its display name at the stop (a worker thread
// draining an actor names the actor). An exception stop fills the exception
// fields.
struct StopInfo {
    int         threadId = 0;
    std::string threadName;
    std::string reason;            // "breakpoint", "step", "pause", "entry", "exception", ...
    bool        isException = false;
    std::string exceptionClass;
    std::string exceptionMessage;
    std::string breakMode;         // "always" (raised) or "unhandled" (uncaught)
    std::string description;       // one line for the stopped event
    std::string stackText;         // the stack at the signal, as text
};

class DebuggerRuntime {
public:
    DebuggerRuntime() = default;
    ~DebuggerRuntime();
    DebuggerRuntime(const DebuggerRuntime&) = delete;
    DebuggerRuntime& operator=(const DebuggerRuntime&) = delete;

    bool attached() const { return attached_.load(std::memory_order_relaxed); }
    void attach()   { attached_.store(true,  std::memory_order_relaxed); }
    void detach()   { attached_.store(false, std::memory_order_relaxed); }

    // `ctx` is the halted thread's own context (the engine that stopped):
    // the prompt blocks in an unmanaged region on it, and `print` runs on it.
    void enterSession(STRuntime& rt, proto::ProtoContext* ctx, DebugFrame frame,
                      const std::string& reason, const StopInfo* info = nullptr);

    enum class Command { Continue, Step, Next, Finish };
    Command lastCommand() const { return lastCommand_; }
    void    setCommand(Command c) { lastCommand_ = c; }

    // Stepping is per thread: every mode below belongs to the calling thread
    // and never makes another thread stop.
    //   * SingleStep / RunToReturn — the `-d` text debugger: stop before the
    //     next instruction (RunToReturn is not distinguished yet).
    //   * StepIn / StepOver / StepOut — the DAP adapter, by source line and
    //     call depth (see stepShouldStop).
    enum class Mode : uint8_t { Free, SingleStep, RunToReturn, StepIn, StepOver, StepOut };
    void setMode(Mode m);            // the calling thread's mode
    Mode mode() const;               // the calling thread's mode

    // The per-instruction hook. The engine calls it before each instruction,
    // only while a debugger is attached (`attached()` is the zero-cost check
    // when detached). Stops for a breakpoint, a step or a pause of this thread.
    void onInstruction(STRuntime& rt, proto::ProtoContext* ctx,
                       const BytecodeModule* m, size_t pc);

    // Exception breakpoints. `setExceptionFilters` takes a bit set of the
    // filters below; `exceptionFilters()` is the cheap test the signal path
    // makes before computing anything.
    enum : unsigned { kFilterRaised = 1u, kFilterUncaught = 2u };
    void     setExceptionFilters(unsigned f) { excFilters_.store(f, std::memory_order_relaxed); }
    unsigned exceptionFilters() const { return excFilters_.load(std::memory_order_relaxed); }
    // Called by `signal` when a language exception is raised, before any
    // handler runs and before anything unwinds. `uncaught` is true when no
    // handler on the thread will take it and its default action ends the
    // activation. Stops when a filter selects it.
    // `exc` is the exception (pinned by the caller); a filter's condition
    // sees it as `ex`.
    void onException(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* exc,
                     const std::string& className, const std::string& message,
                     bool uncaught, bool fromPrimitive);
    // The filters' conditions (Smalltalk expressions; empty: unconditional).
    void setExceptionConditions(const std::string& raised, const std::string& uncaught);

    // Threads. Every thread that runs script code while a debugger is
    // attached gets a stable id (1, 2, ...) the first time it does.
    // `nameCurrentThread` registers the calling thread now, under `name`.
    // `setThreadBaseName` only records the name a later registration uses
    // (a worker sets it at start, before any debugger is attached).
    int         nameCurrentThread(const std::string& name, proto::ProtoContext* ctx = nullptr);
    static void setThreadBaseName(const std::string& name);
    // Pause the thread with debugger id `threadId` at its next instruction.
    // False when no such thread is registered.
    bool        requestPause(int threadId);
    // Stops are suppressed on the calling thread while one of these lives:
    // the debugger evaluating an expression on a stopped thread must not stop
    // on a breakpoint or an exception raised by that expression.
    class SuppressStops {
    public:
        explicit SuppressStops(DebuggerRuntime& d);
        ~SuppressStops();
        SuppressStops(const SuppressStops&) = delete;
        SuppressStops& operator=(const SuppressStops&) = delete;
    private:
        DebugThreadState* ts_;
    };

    // Test/embedder hooks
    void setInputStream(std::istream* is)  { inStream_  = is; }
    void setOutputStream(std::ostream* os) { outStream_ = os; }

    BreakpointTable& breakpoints() { return breakpoints_; }

    // F8-4: evaluate a source expression in the runtime and format the
    // result as a display string. Shared by the `-d` text debugger's
    // `print`/`p` command and the DAP adapter's `evaluate` request so the
    // parse -> compile -> run -> format pipeline lives in exactly one place.
    //
    // `expr` is a source fragment WITHOUT the trailing `.` (it is appended
    // here, matching protoST statement syntax). On success returns true and
    // writes the formatted value to `out`. On a parse / compile / runtime
    // error returns false and writes the diagnostic message to `out`.
    //
    // `frame`, when given, makes the frame's named locals visible to the
    // expression (by value: an assignment does not reach the frame).
    //
    // `ctx` must be the calling thread's own context.
    // The variables of `frame` by name: `self` (when not nil), its named
    // locals and arguments -- a temporary shared with a block read from the
    // captured dict -- and, for a block, the enclosing temporaries it uses.
    // Compiler temporaries (`__...`) and unnamed slots are left out. Reads
    // attributes; call it on the stopped thread's own context.
    static std::vector<FrameVariable> frameVariables(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const DebugFrame& frame);

    // The value of `expr` in `frame` (nullable), with `extra` bound too (it
    // shadows a frame variable of the same name). Stops are suppressed on
    // the calling thread while it runs. False and `error` on a parse,
    // compile or runtime error.
    static bool evaluateValue(STRuntime& rt, proto::ProtoContext* ctx, const std::string& expr,
                              const DebugFrame* frame, const std::vector<FrameVariable>& extra,
                              const proto::ProtoObject*& result, std::string& error);
    // A breakpoint condition: true when it answers true, false when false.
    // Anything else -- an error, a non-Boolean -- answers true (stop) and
    // sets `error`.
    static bool evaluateCondition(STRuntime& rt, proto::ProtoContext* ctx,
                                  const std::string& condition, const DebugFrame& frame,
                                  const std::vector<FrameVariable>& extra, std::string& error);
    // A log message with each `{expr}` replaced by its value in `frame`
    // (`{{` and `}}` are literal braces; an error reads `<error: ...>`).
    static std::string interpolateLogMessage(STRuntime& rt, proto::ProtoContext* ctx,
                                             const std::string& message, const DebugFrame& frame);

    static bool evaluateExpression(STRuntime& rt, proto::ProtoContext* ctx,
                                   const std::string& expr, std::string& out,
                                   const DebugFrame* frame = nullptr);

    // F8-3: a pluggable stop handler. When a frontend is installed, the
    // engine's stop points route through `frontend_->onStopped(...)` instead
    // of the built-in text REPL. Used by the DAP adapter to drive stops over
    // the Debug Adapter Protocol. nullptr (the default) keeps the `-d` text
    // debugger behaviour unchanged.
    void               setFrontend(DebuggerFrontend* f);
    DebuggerFrontend*  frontend() const { return frontend_; }

    // Called by a thread's exit (DebugThreadState's owner).
    void threadExited(DebugThreadState* ts);

private:
    DebugThreadState* currentThread(proto::ProtoContext* ctx);
    DebugThreadState* registerThread(DebugThreadState* ts, proto::ProtoContext* ctx);
    void emitOutput(proto::ProtoContext* ctx, const std::string& category, const std::string& text);
    bool stepShouldStop(DebugThreadState& ts, const BytecodeModule* m, size_t pc, int depth);
    void applyCommand(DebugThreadState& ts, Command cmd, const DebugFrame& frame);
    std::string threadDisplayName(STRuntime& rt, proto::ProtoContext* ctx,
                                  const DebugThreadState& ts);

    std::atomic<bool> attached_{false};
    Command           lastCommand_ = Command::Continue;
    std::atomic<unsigned> excFilters_{0};
    std::istream* inStream_  = nullptr; // nullptr → use std::cin
    std::ostream* outStream_ = nullptr; // nullptr → use std::cout
    BreakpointTable   breakpoints_;
    std::mutex        conditionsMu_;
    std::string       raisedCondition_;
    std::string       uncaughtCondition_;
    std::atomic<DebuggerFrontend*> frontend_{nullptr};  // F8-3: nullptr → text REPL

    // Registered threads by id. Guarded by threadsMu_.
    std::mutex                                       threadsMu_;
    std::unordered_map<int, DebugThreadState*>       threads_;
    int                                              nextThreadId_ = 1;
    // Actor -> number, for thread names ("actor Counter#2").
    std::unordered_map<const void*, int>             actorIds_;
    int                                              nextActorId_ = 1;
};

// F8-3: a stop-handler interface. The debugger calls `onStopped` whenever
// execution halts (breakpoint / step / entry / pause / exception); the
// implementation must block until the user resumes and then return the resume
// command. Several threads may be inside onStopped at once, each for its own
// stop. The DAP adapter implements this to bridge the engine's stop points to
// the protocol.
class DebuggerFrontend {
public:
    virtual ~DebuggerFrontend() = default;
    // Called on the stopped thread. Must block until the user resumes that
    // thread, then return the resume command. `frame` describes where
    // execution halted. `ctx` is the halted thread's own context; a blocking
    // wait must run in an unmanaged region on it.
    virtual DebuggerRuntime::Command onStopped(STRuntime& rt,
                                               proto::ProtoContext* ctx,
                                               const DebugFrame& frame,
                                               const StopInfo& info) = 0;
    // A thread that runs script code was registered (started) or ended.
    // `started` is called on that thread; `exited` may be called from a
    // thread's exit, with no protoCore context. Default: ignore.
    // Text for the client's console (a logpoint, a failing condition),
    // written from a script thread. Default: ignore.
    virtual void onOutput(const std::string& /*category*/, const std::string& /*text*/) {}
    virtual void onThreadEvent(int /*threadId*/, const std::string& /*name*/,
                               bool /*started*/) {}
};

} // namespace protoST
