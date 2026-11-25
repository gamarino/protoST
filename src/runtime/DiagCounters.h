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

// Event ring for the actor scheduling protocol (diagnostic only).
// Word: [63..44] seq (20 bits) | [43..36] thread | [35..28] op | [27..0] arg.
extern "C" std::atomic<std::uint64_t> protost_ring[1024];
extern "C" std::atomic<std::uint64_t> protost_ring_next;
namespace protoST::diag {
enum Op : unsigned {
    evPush = 1,         // sender pushed a message (arg: 0)
    evSchedRead = 2,    // schedule() read __sched__ (arg: value)
    evCas = 3,          // casSchedState (arg: from | to<<4 | ok<<8 | site<<12)
    evTake = 4,         // takeAll batch (arg: size)
    evEnqueue = 5,      // enqueueReady
    evDequeue = 6,      // a worker popped the actor for a turn
    evHasWork = 7,      // finishDrain mailboxHasWork (arg: result | suspended<<1 | path<<4)
    evFdRead = 8,       // finishDrain read __sched__ (arg: value)
    evSpill = 9,        // spill wrote __pending__ (arg: 1 tail, 0 cleared)
};
std::uint64_t threadTag();
inline void ev(unsigned op, std::uint64_t arg) {
    const std::uint64_t seq = protost_ring_next.fetch_add(1, std::memory_order_seq_cst);
    const std::uint64_t w = ((seq & 0xFFFFF) << 44) | ((threadTag() & 0xFF) << 36)
                          | ((std::uint64_t(op) & 0xFF) << 28) | (arg & 0xFFFFFFF);
    protost_ring[seq & 1023].store(w, std::memory_order_seq_cst);
}
}
