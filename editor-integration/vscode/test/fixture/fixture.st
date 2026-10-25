Object subclass: #Calc.
Calc >> twice: n
  | doubled |
  doubled := n * 2.
  ^ doubled.
Calc >> risky: n
  Error signal: 'boom ', n printString.
  ^ n.
c := Calc new.
'e2e-before' displayNl.
(c twice: 21) printNl.
r := [ c risky: 7 ] on: Error do: [ :e | 'caught ', e messageText ].
r displayNl.
'e2e-after' displayNl.
