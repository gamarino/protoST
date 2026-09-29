"EXPECT-ERROR: cannot assign to the argument 'i'"
"Block arguments are read-only, including the loop variable of an inlined to:do:."
| s |
s := 0.
1 to: 3 do: [:i | i := i + 1. s := s + i].
s
