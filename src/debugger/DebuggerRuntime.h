#pragma once
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>
#include "BreakpointTable.h"
namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;
class BytecodeModule;

struct DebugFrame {
    const BytecodeModule*                  module = nullptr;
    size_t                                 pc = 0;
    int                                    frameDepth = 0;
    std::vector<const proto::ProtoObject*> stack;
    std::vector<const proto::ProtoObject*> locals;
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

class DebuggerRuntime {
public:
    bool attached() const { return attached_.load(std::memory_order_relaxed); }
    void attach()   { attached_.store(true,  std::memory_order_relaxed); }
    void detach()   { attached_.store(false, std::memory_order_relaxed); }

    void enterSession(STRuntime& rt, DebugFrame frame, const std::string& reason);

    enum class Command { Continue, Step, Next, Finish };
    Command lastCommand() const { return lastCommand_; }
    void    setCommand(Command c) { lastCommand_ = c; }

    enum class Mode : uint8_t { Free, SingleStep, RunToReturn };
    void setMode(Mode m) { mode_.store(static_cast<uint8_t>(m), std::memory_order_relaxed); }
    Mode mode() const    { return static_cast<Mode>(mode_.load(std::memory_order_relaxed)); }

    // Test/embedder hooks
    void setInputStream(std::istream* is)  { inStream_  = is; }
    void setOutputStream(std::ostream* os) { outStream_ = os; }

    BreakpointTable& breakpoints() { return breakpoints_; }

    // F8-3: a pluggable stop handler. When a frontend is installed, the
    // engine's stop points route through `frontend_->onStopped(...)` instead
    // of the built-in text REPL. Used by the DAP adapter to drive stops over
    // the Debug Adapter Protocol. nullptr (the default) keeps the `-d` text
    // debugger behaviour unchanged.
    void               setFrontend(DebuggerFrontend* f) { frontend_ = f; }
    DebuggerFrontend*  frontend() const { return frontend_; }

private:
    std::atomic<bool> attached_{false};
    Command           lastCommand_ = Command::Continue;
    std::atomic<uint8_t> mode_{static_cast<uint8_t>(Mode::Free)};
    std::istream* inStream_  = nullptr; // nullptr → use std::cin
    std::ostream* outStream_ = nullptr; // nullptr → use std::cout
    BreakpointTable   breakpoints_;
    DebuggerFrontend* frontend_ = nullptr;  // F8-3: nullptr → text REPL
};

// F8-3: a stop-handler interface. The debugger calls `onStopped` whenever
// execution halts (breakpoint / step / entry); the implementation must block
// until the user resumes and then return the resume command. The DAP adapter
// implements this to bridge the engine's stop points to the protocol.
class DebuggerFrontend {
public:
    virtual ~DebuggerFrontend() = default;
    // Called when execution stops. Must block until the user resumes, then
    // return the resume command. `frame` describes where execution halted.
    virtual DebuggerRuntime::Command onStopped(STRuntime& rt,
                                               const DebugFrame& frame,
                                               const std::string& reason) = 0;
};

} // namespace protoST
