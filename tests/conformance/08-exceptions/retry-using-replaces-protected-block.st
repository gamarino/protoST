"EXPECT: replacement #('a' 'b' 'b') 2"
"§8.4: retryUsing: aBlock makes aBlock the protected block of the handler's
 on:do: and evaluates it under the same handler, as Pharo's
 Exception>>retryUsing: (handlerContext restartWithNewReceiver:). The
 replacement signals 'b', which the SAME handler receives: it retries once,
 which evaluates the replacement again (not the original block), and then
 resumes. The original block ran once. Before the fix the replacement ran
 inside the handler, its signal went to the enclosing handler and the
 answer was 'outer took b'."
| n log originalRuns r |
n := 0.
originalRuns := 0.
log := OrderedCollection new.
r := [[originalRuns := originalRuns + 1. Warning signal: 'a'. 'original']
        on: Warning
        do: [:e |
          log add: e messageText.
          n := n + 1.
          n = 1 ifTrue: [e retryUsing: [Warning signal: 'b'. 'replacement']].
          n = 2 ifTrue: [e retry].
          e resume: nil]]
      on: Warning
      do: [:e | 'outer took ' , e messageText].
r , ' ' , log asArray printString , ' ' , (originalRuns + 1) printString
