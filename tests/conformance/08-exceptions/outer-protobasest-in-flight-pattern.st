"EXPECT: 42 false|aborted true|retried true|0 true|fell true"
"§8.4: the pattern of protoBaseST's newTransactionDo:. An inner catch-all
 handler marks a flag, asks the enclosing handler with outer, clears the flag
 and resumes with the answer. If the enclosing handler resumes, outer answers
 inside the inner handler and the flag is cleared; if it returns, retries or
 falls off its end, the unwind passes through the inner handler and the flag
 stays set."
| inFlight run n a b c d e |
run := [:outerHandler :exceptionClass |
  inFlight := false.
  n := 0.
  [n := n + 1.
   [n = 1 ifTrue: [(exceptionClass signal: 'w') + 1] ifFalse: ['retried']]
     on: Exception
     do: [:ex | | r |
       inFlight := true.
       r := ex outer.
       inFlight := false.
       ex resume: r]]
    on: exceptionClass do: outerHandler].
a := run value: [:ex | ex resume: 41] value: Warning.
a := a printString , ' ' , inFlight printString.
b := run value: [:ex | ex return: 'aborted'] value: Warning.
b := b , ' ' , inFlight printString.
c := run value: [:ex | ex retry] value: Warning.
c := c , ' ' , inFlight printString.
d := run value: [:ex | ex return: 0] value: Error.
d := d printString , ' ' , inFlight printString.
e := run value: [:ex | 'fell'] value: Warning.
e := e , ' ' , inFlight printString.
a , '|' , b , '|' , c , '|' , d , '|' , e
