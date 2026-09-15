"EXPECT: 18"
"§6.4 + D29: two inlined to:do: loops around a do: whose block evaluates"
"another one-argument block."
s := 0.
1 to: 2 do: [ :i |
  1 to: 3 do: [ :j |
    #(1 2) do: [ :x | s := s + ([ :a | a ] value: x) ] ] ].
s.
