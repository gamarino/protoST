"EXPECT: 20"
"§10 Atom: a shared mutable cell with optimistic-concurrency compare-and-swap.
 Created with `on:`, read with `value`, updated with `value:ifCurrent:` — the
 raw CAS, which installs the new value only when the cell still holds the
 expected one. The winning CAS (10 -> 20) succeeds; the losing CAS (expecting
 10, no longer current) writes nothing, so the cell ends at 20."
a := Atom on: 10.
a value: 20 ifCurrent: 10.
a value: 99 ifCurrent: 10.
a value printNl.
