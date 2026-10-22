"throw_catch: signal an Error and catch it in the same method, 200000 times."
Object subclass: #Probe.
Probe >> run: n
  | c |
  c := 0.
  1 to: n do: [ :i | [ Error new signal: 'x' ] on: Error do: [ :e | c := c + 1 ] ].
  ^ c
Transcript showCr: 'CHECK ' , (Probe new run: 200000) printString.
