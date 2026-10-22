"throw_across5: signal 5 frames below the handler, 100000 times."
Object subclass: #Probe.
Probe >> deep: d
  d = 0 ifTrue: [ ^ Error new signal: 'deep' ].
  ^ 1 + (self deep: d - 1)
Probe >> run: n
  | c |
  c := 0.
  1 to: n do: [ :i | [ self deep: 5 ] on: Error do: [ :e | c := c + 1 ] ].
  ^ c
Transcript showCr: 'CHECK ' , (Probe new run: 100000) printString.
