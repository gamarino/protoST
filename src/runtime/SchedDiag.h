// F6 v3 E2 — scheduler diagnostics.
//
// A zero-overhead-when-off tracing facility for the cooperative actor
// scheduler. Every component boundary (yield, future resolution, drainOne
// pop / resume, worker cv.wait, schedule) emits one line when the
// PROTOST_SCHED_DIAG environment variable is set to a non-empty value.
//
// When the variable is unset the SCHED_DIAG macro expands to a branch on a
// cached `bool` that is `false`, so the cost is a single predicted-not-taken
// test per call site — no string formatting, no I/O.
//
// The flag is read exactly once (first SCHED_DIAG hit) and cached. Output
// goes to stderr, prefixed with the calling OS thread id so the interleaving
// of worker threads is legible.
#ifndef PROTOST_SCHED_DIAG_H
#define PROTOST_SCHED_DIAG_H

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace protoST {

inline bool schedDiagEnabled() {
    static const bool enabled = [] {
        const char* e = std::getenv("PROTOST_SCHED_DIAG");
        return e != nullptr && e[0] != '\0';
    }();
    return enabled;
}

inline std::mutex& schedDiagMutex() {
    static std::mutex m;
    return m;
}

} // namespace protoST

// Usage: SCHED_DIAG("yield actor=" << actor << " fut=" << fut);
#define SCHED_DIAG(expr)                                                      \
    do {                                                                      \
        if (::protoST::schedDiagEnabled()) {                                  \
            std::ostringstream _sd_oss;                                       \
            _sd_oss << "[SCHED t" << std::this_thread::get_id() << "] "        \
                    << expr << '\n';                                          \
            std::lock_guard<std::mutex> _sd_lk(::protoST::schedDiagMutex());   \
            std::cerr << _sd_oss.str();                                       \
            std::cerr.flush();                                                \
        }                                                                     \
    } while (0)

#endif // PROTOST_SCHED_DIAG_H
