"EXPECT: 3"
"§6.4 + D29: a to:do: body evaluates a block literal. The compiler inlines the"
"loop; a block literal in the inlined body used to crash the compiler."
n := 0.
1 to: 3 do: [ :i | n := [ n + 1 ] value ].
n.
