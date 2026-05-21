"EXPECT: 2"
"§9.2: do:separatedBy: runs the separator block between elements (3 elems -> 2 separators)"
seps := 0.
#(1 2 3) do: [ :e | e ] separatedBy: [ seps := seps + 1 ].
seps.
