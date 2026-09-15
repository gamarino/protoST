"EXPECT: 21"
"§6.4 + D29: inside a method, nested to:do: loops whose inner bound is the"
"outer loop variable, with a one-argument block evaluated in the inner body."
Object subclass: #Triangle.
Triangle >> count: n
  | s |
  s := 0.
  1 to: n do: [ :i | 1 to: i do: [ :j | s := s + ([ :e | e ] value: 1) ] ].
  ^ s.
Triangle new count: 6.
