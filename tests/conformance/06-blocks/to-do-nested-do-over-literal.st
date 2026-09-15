"EXPECT: 6"
"§6.4 + D29: a do: over an array literal nested in a to:do: body; the inner"
"block updates a module-level variable."
s := 0.
1 to: 2 do: [ :i | #(1 2) do: [ :x | s := s + x ] ].
s.
