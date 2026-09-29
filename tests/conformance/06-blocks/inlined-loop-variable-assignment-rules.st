"EXPECT-ERROR: cannot assign to the argument 'i'"
"The loop variable of an outer inlined to:do: is read-only in nested loops
 too; a block temporary that reuses its name is a distinct variable and may
 be assigned (checked by the first loop, which must compile)."
1 to: 2 do: [:i | #(1 2) do: [:x | | i | i := x]].
1 to: 3 do: [:i | 1 to: 3 do: [:j | i := 10]].
