"EXPECT: 42 42"
"§10 A wait inside a timer callback (a thenDo: block run by a pool worker,
 with no current actor) must not take the main thread's wait slot. The main
 thread waits on `a`; the callback of a 50 ms timer waits on `b`, which a
 second timer resolves at 100 ms, and then resolves `a`. Correctness does not
 depend on timing: the program answers 42 42 in every interleaving. The
 timings only make the interleaving that exposed the defect the likely one
 (the main thread parked on `a` before the callback waits on `b`); with the
 defect the program hangs, and the runner's timeout reports it."
| a b inner |
a := Future new.
b := Future new.
inner := Future new.
(Future after: 50) thenDo: [:x | | v | v := b wait. inner resolve: v. a resolve: v].
(Future after: 100) thenDo: [:x | b resolve: 42].
a wait printString , ' ' , inner wait printString
