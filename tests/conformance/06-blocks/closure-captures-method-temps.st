"EXPECT: 15"
"§6.3: a block captures the enclosing method's temporaries by reference"
Object subclass: #Summer.
Summer >> sumUpTo: n
  | total |
  total := 0.
  1 to: n do: [ :i | total := total + i ].
  ^ total.
Summer new sumUpTo: 5.
