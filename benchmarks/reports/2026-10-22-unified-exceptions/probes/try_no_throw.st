"try_no_throw: enter an on:do: whose body does not signal, 200000 times."
Object subclass: #Probe.
Probe >> run: n
  | c |
  c := 0.
  1 to: n do: [ :i | [ c := c + 1 ] on: Error do: [ :e | c := c - 1 ] ].
  ^ c
Transcript showCr: 'CHECK ' , (Probe new run: 200000) printString.
