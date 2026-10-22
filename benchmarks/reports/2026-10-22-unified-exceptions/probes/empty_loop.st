"empty_loop: the loop alone, 2000000 iterations."
Object subclass: #Probe.
Probe >> run: n
  | c |
  c := 0.
  1 to: n do: [ :i | c := c + 1 ].
  ^ c
Transcript showCr: 'CHECK ' , (Probe new run: 2000000) printString.
