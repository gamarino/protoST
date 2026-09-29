#pragma once

#include <atomic>

namespace protoST {

// A counting semaphore whose waiters never sleep while a permit is available.
//
// It replaces std::counting_semaphore, whose libstdc++ 13 implementation can
// lose a wakeup (S19): _M_release notifies only when the count was 0, and a
// waiter that read a non-zero count, lost the compare-and-swap for that permit
// and then went to sleep on the stale non-zero value misses every later
// release that finds the count above 0. Observed as a worker parked for ever
// in acquire() with a permit available, so ~STRuntime waited on its join.
//
// Here a waiter sleeps only on the value 0 — the kernel refuses the sleep if
// the count has changed — and every release notifies. The count is an int so
// that std::atomic::wait maps onto the platform wait (a futex on Linux).
class Semaphore {
public:
    void release(int permits = 1) noexcept {
        count_.fetch_add(permits, std::memory_order_release);
        if (permits == 1) count_.notify_one();
        else              count_.notify_all();
    }

    void acquire() noexcept {
        for (;;) {
            int c = count_.load(std::memory_order_acquire);
            while (c > 0) {
                if (count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire,
                                                 std::memory_order_relaxed))
                    return;
            }
            count_.wait(0, std::memory_order_acquire);
        }
    }

private:
    std::atomic<int> count_{0};
};

} // namespace protoST
