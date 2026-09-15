"EXPECT: 6"
"§6.3 + D29: inside a method, a do: nested in a to:do: body updates a method"
"temporary."
Object subclass: #NestedLoop.
NestedLoop >> run
  | s |
  s := 0.
  1 to: 2 do: [ :i | #(1 2) do: [ :x | s := s + x ] ].
  ^ s.
NestedLoop new run.
