"EXPECT: 900"
"Recursion through a block primitive works to a useful depth."
Object subclass: #T.
T >> walk: n
  | s |
  n = 0 ifTrue: [^ 0].
  s := 0.
  #(1) do: [:x | s := s + x + (self walk: n - 1)].
  ^ s.

T new walk: 900.
