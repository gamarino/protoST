"EXPECT-ERROR: cannot assign to the argument 'e'"
"A block argument of an ordinary block is read-only."
#(1 2) do: [:e | e := 0].
