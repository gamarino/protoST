#pragma once
// DIAGNOSTIC ONLY (branch diag/actor-stress-macos-instrumented, never merged):
// a flat array of counters read from a hung process with lldb
// (`memory read` of the C symbol protost_diag).
#include <atomic>
#include <cstdint>

extern "C" std::atomic<std::intptr_t> protost_diag[16];

namespace protoST::diag {
enum : int {
    kMarkNonNull = 0,     // markMainWaitingOn(non-null) calls
    kMarkedFuture = 1,    // last future registered by markMainWaitingOn
    kAcqEnter = 2,        // acquireMainWait entries
    kAcqExit = 3,         // acquireMainWait returns
    kNotifyCalls = 4,     // notifyMainWaiterIfFor calls
    kNotifyMatch = 5,     // notifyMainWaiterIfFor matches (mainWaitSem releases)
    kNotifyLastFut = 6,   // last future passed to notifyMainWaiterIfFor
    kNotifyLastSeen = 7,  // last mainWaitingOn value notify loaded
    kSettleClaims = 8,    // settleFuture claims won
    kSettledMarked = 9,   // settle of the currently marked future: state stored
    kWaitLoopZero = 10,   // main wait loop iterations that read state 0
    kWorkerRelease = 11,  // workerSem releases from enqueueReady/enqueueTask
    kMainSemAddr = 12,    // &mainWaitSem
    kWorkerSemAddr = 13,  // &workerSem
    kLastStateRead = 14,  // last state the main wait loop read
    kMagic = 15,          // 0x5157 when the array is live
};
inline void bump(int i) { protost_diag[i].fetch_add(1, std::memory_order_seq_cst); }
inline void put(int i, std::intptr_t v) { protost_diag[i].store(v, std::memory_order_seq_cst); }
inline void put(int i, const void* p) { put(i, reinterpret_cast<std::intptr_t>(p)); }
}
