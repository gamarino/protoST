"EXPECT: nil true"
"§10 Future after: ms answers a Future that the timer service resolves with
 nil once ms milliseconds of the monotonic clock have passed. Timing: only
 the lower bound (at least 50 ms elapsed) is asserted; no upper bound, so a
 loaded machine cannot fail it."
| value elapsed |
elapsed := Time millisecondsToRun: [value := (Future after: 50) wait].
value printString , ' ' , (elapsed >= 50) printString
