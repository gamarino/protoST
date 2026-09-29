"EXPECT-ERROR: undeclared"
"An undeclared name that an inner block also assigns is still a compile error:
 capture by a block must not turn a typo into a silent method local."

Object subclass: #C.

C >> sum: rounds
  total := 0.
  1 to: rounds do: [ :i | total := total + i ].
  ^ total.

(C new sum: 3) printNl.
