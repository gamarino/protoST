Object subclass: #Adders.

Adders >> adderFor: n
  ^ [ :x | x + n ].

| add5 |
add5 := Adders new adderFor: 5.
(add5 value: 10) printNl.
