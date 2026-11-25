#pragma once
// DIAGNOSTIC ONLY (branch diag/actor-stress-macos-instrumented, never merged):
// a flat array of counters read from a hung process with lldb
// (`memory read` of the C symbol protost_diag).
#include <atomic>
#include <cstdint>

extern "C" std::atomic<std::intptr_t> protost_diag[32];
extern bool protost_diag_fence;

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
    // Actor protocol, relaxed (no ordering added to the code under test):
    kPushes = 16,         // messages pushed onto a mailbox
    kSchedSaw2 = 17,      // schedule() returned on a plain read of 2
    kSched1to2 = 18,      // schedule() CAS 1->2 succeeded
    kSched0to1 = 19,      // schedule() CAS 0->1 succeeded (enqueued)
    kSchedLast = 20,      // last schedule outcome: 2 saw2, 12 cas1->2, 1 cas0->1
    kTaken = 21,          // messages taken by takeAll
    kFdStaleRel = 22,     // finishDrain 2->1, mailbox empty, 1->0 released
    kFdPlainRel = 23,     // finishDrain 1->0 released, mailbox empty
    kFdLast = 24,         // last finishDrain outcome: 1 requeue(2) 2 staleRel 3 plainRel 4 backlog
    kTurns = 25,          // turns (actor dequeued)
    kFenceOn = 26,        // PROTOST_DIAG_FENCE set
};
inline void rbump(int i) { protost_diag[i].fetch_add(1, std::memory_order_relaxed); }
inline void rput(int i, std::intptr_t v) { protost_diag[i].store(v, std::memory_order_relaxed); }
inline void bump(int i) { protost_diag[i].fetch_add(1, std::memory_order_seq_cst); }
inline void put(int i, std::intptr_t v) { protost_diag[i].store(v, std::memory_order_seq_cst); }
inline void put(int i, const void* p) { put(i, reinterpret_cast<std::intptr_t>(p)); }
}
