#include "Interrupt.h"
#include "protoCore.h"

#include <atomic>
#include <csignal>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace protoST {

namespace {
std::atomic<bool> g_pending{false};
std::atomic<const proto::ProtoThread*> g_armedThread{nullptr};

#if defined(_WIN32)
// Set by the handler, waited for by the REPL's console read (an aborted
// ReadConsoleW returns before the handler thread has run).
HANDLE g_wakeEvent = nullptr;
#else
// The self-pipe: the handler writes a byte, which wakes the REPL's poll on
// standard input. Both ends are non-blocking, so the handler never blocks.
int g_wakePipe[2] = {-1, -1};
#endif

void wake() {
#if defined(_WIN32)
    if (g_wakeEvent) ::SetEvent(g_wakeEvent);
#else
    if (g_wakePipe[1] >= 0) {
        const int saved = errno;
        const char b = 1;
        [[maybe_unused]] const ssize_t n = ::write(g_wakePipe[1], &b, 1);
        errno = saved;
    }
#endif
}

extern "C" void onSigint(int) {
    if (g_pending.exchange(true, std::memory_order_relaxed)) {
        // The first Ctrl-C was never taken: stop the process, as the
        // platform's default action for Ctrl-C would.
#if defined(_WIN32)
        // What Windows' default console handler does: the CRT's raise()
        // with the default action would end the process with status 3.
        ::ExitProcess(STATUS_CONTROL_C_EXIT);
#else
        std::signal(SIGINT, SIG_DFL);
        std::raise(SIGINT);
        return;
#endif
    }
#if defined(_WIN32)
    // The Windows CRT resets SIGINT to SIG_DFL before calling a handler; put
    // this one back so the next Ctrl-C is seen as it is elsewhere.
    std::signal(SIGINT, onSigint);
#endif
    wake();
}
} // namespace

void armInterrupts(proto::ProtoContext* ctx) {
    g_armedThread.store(ctx ? ctx->thread : nullptr, std::memory_order_relaxed);
#if defined(_WIN32)
    if (!g_wakeEvent) g_wakeEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
#else
    if (g_wakePipe[0] < 0 && ::pipe(g_wakePipe) == 0) {
        for (int fd : g_wakePipe) {
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        }
    }
#endif
    std::signal(SIGINT, onSigint);
}

void clearPendingInterrupt() {
    g_pending.store(false, std::memory_order_relaxed);
}

bool takePendingInterrupt() {
    // The wake-up is consumed first: a Ctrl-C arriving in between is then
    // either taken below or leaves a wake-up behind (a spurious wake later,
    // never a lost one).
#if defined(_WIN32)
    if (g_wakeEvent) ::ResetEvent(g_wakeEvent);
#else
    char buf[64];
    while (g_wakePipe[0] >= 0 && ::read(g_wakePipe[0], buf, sizeof buf) > 0) {}
#endif
    return g_pending.exchange(false, std::memory_order_relaxed);
}

#if defined(_WIN32)
void* interruptWakeEvent() { return g_wakeEvent; }
#else
int interruptWakeFd() { return g_wakePipe[0]; }
#endif

void pollInterrupt(proto::ProtoContext* ctx) {
    if (!g_pending.load(std::memory_order_relaxed)) return;
    const proto::ProtoThread* armed = g_armedThread.load(std::memory_order_relaxed);
    if (!armed || !ctx || ctx->thread != armed) return;
    g_pending.store(false, std::memory_order_relaxed);
    throw InterruptSignal();
}

} // namespace protoST
