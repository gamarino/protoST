#pragma once
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>
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

private:
    std::atomic<bool> attached_{false};
    Command           lastCommand_ = Command::Continue;
    std::atomic<uint8_t> mode_{static_cast<uint8_t>(Mode::Free)};
    std::istream* inStream_  = nullptr; // nullptr → use std::cin
    std::ostream* outStream_ = nullptr; // nullptr → use std::cout
};

} // namespace protoST
