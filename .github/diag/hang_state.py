# DIAGNOSTIC ONLY: run inside `lldb -p PID` on a hung protost.
# Prints the protost_diag counters, the count of every protoST::Semaphore a
# thread is blocked in, and for each thread in __ulock_wait the libc++
# contention-table entry it sleeps on (x1 = address, x2 = expected value).
import lldb

NAMES = ["markNonNull", "markedFuture", "acqEnter", "acqExit", "notifyCalls",
         "notifyMatch", "notifyLastFut", "notifyLastSeen", "settleClaims",
         "settledMarked", "waitLoopZero", "workerRelease", "mainSemAddr",
         "workerSemAddr", "lastStateRead", "magic"]


def rd(proc, addr, size):
    err = lldb.SBError()
    v = proc.ReadUnsignedFromMemory(addr, size, err)
    return v if err.Success() else None


def diag_addr(target):
    for name in ("protost_diag",):
        scl = target.FindSymbols(name)
        for i in range(scl.GetSize()):
            a = scl.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
            if a != lldb.LLDB_INVALID_ADDRESS:
                return a
        v = target.FindFirstGlobalVariable(name)
        if v.IsValid():
            return v.GetLoadAddress()
    return None


OPS = {1: "push", 2: "schedRead", 3: "cas", 4: "take", 5: "enqueue", 6: "dequeue",
       7: "hasWork", 8: "fdRead", 9: "spill"}


def sym_addr(target, name):
    scl = target.FindSymbols(name)
    for i in range(scl.GetSize()):
        a = scl.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
        if a != lldb.LLDB_INVALID_ADDRESS:
            return a
    return None


def dump_ring(target, proc, last=160):
    base = sym_addr(target, "protost_ring")
    nxt = sym_addr(target, "protost_ring_next")
    if not base or not nxt:
        print("ring: symbols not found"); return
    n = rd(proc, nxt, 8)
    print("ring: next=%s" % n)
    start = max(0, n - last)
    for seq in range(start, n):
        w = rd(proc, base + 8 * (seq % 1024), 8)
        if w is None:
            continue
        s, tid, op, arg = w >> 44, (w >> 36) & 0xFF, (w >> 28) & 0xFF, w & 0xFFFFFFF
        desc = OPS.get(op, str(op))
        if op == 3:
            desc += " %d->%d ok=%d" % (arg & 0xF, (arg >> 4) & 0xF, (arg >> 8) & 1)
        elif op == 7:
            desc += " result=%d suspended=%d path=%d" % (arg & 1, (arg >> 1) & 1, arg >> 4)
        else:
            desc += " %d" % arg
        print("  ev seq=%d(%d) t%d %s" % (seq, s, tid, desc))


def run(debugger):
    target = debugger.GetSelectedTarget()
    proc = target.GetProcess()
    print("=== HANG STATE")
    base = diag_addr(target)
    print("protost_diag @", hex(base) if base else None)
    vals = []
    if base:
        for i, n in enumerate(NAMES):
            v = rd(proc, base + 8 * i, 8)
            vals.append(v)
            print("  diag[%2d] %-15s = %s (%s)" % (i, n, v, hex(v) if v is not None else "?"))
    if len(vals) == 16:
        for label, a in (("mainWaitSem", vals[12]), ("workerSem", vals[13])):
            if a:
                print("  %s count_ = %s" % (label, rd(proc, a, 4)))
    for t in proc:
        f0 = t.GetFrameAtIndex(0)
        fn = f0.GetFunctionName() or f0.GetSymbol().GetName() or "?"
        line = "thread #%d tid=%d frame0=%s" % (t.GetIndexID(), t.GetThreadID(), fn)
        if "__ulock_wait" in fn:
            regs = {}
            for r in ("x0", "x1", "x2", "x3", "x16"):
                rv = f0.FindRegister(r)
                regs[r] = rv.GetValueAsUnsigned() if rv.IsValid() else None
            line += " regs=" + " ".join("%s=%s" % (k, hex(v) if v is not None else "?") for k, v in regs.items())
            a = regs.get("x1")
            if a:
                line += " | *x1(8)=%s *(x1-8)(8)=%s *(x1+8)(8)=%s *x1(4)=%s" % (
                    rd(proc, a, 8), rd(proc, a - 8, 8), rd(proc, a + 8, 8), rd(proc, a, 4))
        for i in range(min(t.GetNumFrames(), 12)):
            fr = t.GetFrameAtIndex(i)
            n = fr.GetFunctionName() or ""
            if "Semaphore::acquire" in n:
                th = fr.FindVariable("this")
                a = th.GetValueAsUnsigned() if th.IsValid() else 0
                line += " | Semaphore(this=%s).count_=%s caller=%s" % (
                    hex(a), rd(proc, a, 4) if a else None,
                    t.GetFrameAtIndex(i + 1).GetFunctionName())
                break
        print(line)
    dump_ring(target, proc)
    print("=== END HANG STATE")
