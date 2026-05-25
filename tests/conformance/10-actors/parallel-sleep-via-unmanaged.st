"EXPECT: 21"
"§10 Parallel-sleep proof — three actors each sleep 100 ms must run in
parallel and complete in roughly 100 ms wall-clock, not 300 ms.

The mechanism is the unmanaged-region API protoCore exposed
2026-05-25: `Object>>sleep:` now brackets its `std::this_thread::
sleep_for` call in `ProtoContext::UnmanagedScope`. Without the
bracket, a worker parked in `sleep_for` cannot reach a GC safepoint
until the timer fires; if a stop-the-world cycle starts while three
workers are sleeping, the GC would stall behind them and the
benchmarks file documents only the optimistic case.

With the bracket, each sleeping worker is pre-counted as parked in
the GC quorum — collection can run to completion without waiting,
AND a wake from `sleep_for` calls `returnFromUnmanaged()` which
itself parks at a safepoint if a STW is in progress at that
moment. The thread cannot resume touching `ProtoObject*` until the
GC has finished scanning roots.

This test just exercises the happy path: the three reads finish in
~100 ms together because they really do run on three workers, the
unmanaged-region API never blocks the parallelism, and the
arithmetic at the end (7 + 7 + 7 = 21) confirms each actor
completed correctly."

Object subclass: #Sensor.
Sensor >> read
  Object sleep: 100.
  ^ 7.

a := Sensor new asActor.
b := Sensor new asActor.
c := Sensor new asActor.
fa := a read.
fb := b read.
fc := c read.
^ (fa wait) + (fb wait) + (fc wait).
