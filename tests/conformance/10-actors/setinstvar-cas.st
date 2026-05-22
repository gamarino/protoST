"EXPECT: 2"
"§10 setInstVar:from:to: — the raw optimistic-concurrency compare-and-swap on
 a single instance variable, the unwrapped form of the CAS the Atom class
 wraps. The first CAS (1 -> 2) matches the current value and succeeds; the
 second expects 1, no longer current, and fails — writing nothing. The
 instance variable ends at 2."
Object subclass: #Box instanceVariableNames: 'val'.
Box >> val  ^ val.
Box >> setVal: x  val := x. ^ self.
b := Box new.
b setVal: 1.
b setInstVar: #val from: 1 to: 2.
b setInstVar: #val from: 1 to: 9.
b val printNl.
