"EXPECT: 6"
"§6.4 + D29: both bounds of a to:do: are computed by evaluating blocks."
s := 0.
[ 1 ] value to: [ 3 ] value do: [ :i | s := s + i ].
s.
