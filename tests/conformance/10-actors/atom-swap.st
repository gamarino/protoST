"EXPECT: 15"
"§10 Atom>>swap: — the read-modify-CAS retry loop. It reads the current value,
 applies the block to compute the new value, and compare-and-swaps it in;
 on a lost race it re-reads and retries. Here the cell goes 10 -> 15."
a := Atom on: 10.
a swap: [ :v | v + 5 ].
a value printNl.
