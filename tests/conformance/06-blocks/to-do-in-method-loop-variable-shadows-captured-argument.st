"EXPECT: 6 5"
"§6.3: inside a method, a to:do: block argument named like a captured method"
"argument is a distinct variable. The loop sums 1..3; the block reads the"
"argument. Before the D29 fix the inlined loop body read the argument (15 5)."
Object subclass: #Shadow.
Shadow >> run: i
  | b s |
  b := [ i ].
  s := 0.
  1 to: 3 do: [ :i | s := s + i ].
  ^ s printString, ' ', b value printString.
Shadow new run: 5.
