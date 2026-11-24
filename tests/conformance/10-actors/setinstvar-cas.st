"EXPECT: 2"
"§10 compareAndSet:from:to: — the raw optimistic-concurrency compare-and-swap
 on a single instance variable, the unwrapped form of the CAS the Atom class
 wraps (explicit-state spec §6.4; it replaces the alias deprecated in
 0.17.0). The first CAS (1 -> 2) matches the current value and
 succeeds; the second expects 1, no longer current, and fails — writing
 nothing. The instance variable ends at 2."
Object subclass: #Box instanceVariableNames: 'val'.
Box >> val  ^ val.
Box >> setVal: x  val := x. ^ self.
b := Box new.
b setVal: 1.
b compareAndSet: #val from: 1 to: 2.
b compareAndSet: #val from: 1 to: 9.
b val printNl.
