"EXPECT: #(5 #('outer handler' 'inner ensure') 8 #('outer handler' 'inner ensure') 'nil')"
"§8.4: ex pass yields to the enclosing handler as if the inner one had not
 handled the exception. The enclosing handler runs on top of the inner one
 (Pharo's Exception>>pass sends handleSignal: to the next handler context
 without unwinding); only its resume: or return: then unwinds the inner
 handler, so the inner handler's ensure: block runs after the enclosing
 handler. A resume: answers at the original signal (4 + 1). Before the fix the
 inner handler was unwound first and its ensure: block ran first."
| log r1 r2 r3 |
log := OrderedCollection new.
r1 := [[(Warning signal: 'w') + 1]
         on: Warning
         do: [:e | [e pass. 'unreached'] ensure: [log add: 'inner ensure']]]
       on: Warning
       do: [:e | log add: 'outer handler'. e resume: 4].
r1 := {r1. log asArray}.
log := OrderedCollection new.
r2 := [[(Warning signal: 'w') + 1]
         on: Warning
         do: [:e | [e pass. 'unreached'] ensure: [log add: 'inner ensure']]]
       on: Warning
       do: [:e | log add: 'outer handler'. e return: 8].
r2 := {r2. log asArray}.
r3 := [(Warning signal: 'w') printString] on: Warning do: [:e | e pass. 'unreached'].
{r1 first. r1 last. r2 first. r2 last. r3}
