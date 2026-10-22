"zerodiv: native integer division by zero (1 // z), caught as ZeroDivide, 200000 times."
Object subclass: #Probe.
Probe >> run: n zero: z
  | c |
  c := 0.
  1 to: n do: [ :i | [ c := c + (1 // z) ] on: ZeroDivide do: [ :e | c := c + 1 ] ].
  ^ c
Transcript showCr: 'CHECK ' , (Probe new run: 200000 zero: 0) printString.
