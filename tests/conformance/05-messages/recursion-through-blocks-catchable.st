"EXPECT: caught"
"Unbounded recursion through a block primitive (do:) nests native frames; it
 must end in a catchable Error, never a crash."
Object subclass: #T.
T >> walk: n
  | s |
  s := 0.
  #(1) do: [:x | s := s + x + (self walk: n + 1)].
  ^ s.

[T new walk: 0] on: Error do: [:e | 'caught'].
