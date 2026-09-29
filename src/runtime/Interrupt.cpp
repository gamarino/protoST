#include "Interrupt.h"
#include "protoCore.h"

#include <atomic>
#include <csignal>

namespace protoST {

namespace {
std::atomic<bool> g_pending{false};
std::atomic<const proto::ProtoThread*> g_armedThread{nullptr};

extern "C" void onSigint(int) {
    if (g_pending.exchange(true, std::memory_order_relaxed)) {
        // The first Ctrl-C was never taken: stop the process.
        std::signal(SIGINT, SIG_DFL);
        std::raise(SIGINT);
    }
}
} // namespace

void armInterrupts(proto::ProtoContext* ctx) {
    g_armedThread.store(ctx ? ctx->thread : nullptr, std::memory_order_relaxed);
    std::signal(SIGINT, onSigint);
}

void clearPendingInterrupt() {
    g_pending.store(false, std::memory_order_relaxed);
}

void pollInterrupt(proto::ProtoContext* ctx) {
    if (!g_pending.load(std::memory_order_relaxed)) return;
    const proto::ProtoThread* armed = g_armedThread.load(std::memory_order_relaxed);
    if (!armed || !ctx || ctx->thread != armed) return;
    g_pending.store(false, std::memory_order_relaxed);
    throw InterruptSignal();
}

} // namespace protoST
