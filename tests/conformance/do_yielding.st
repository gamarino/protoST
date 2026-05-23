"EXPECT: 60"
"do_yielding.st - basic correctness of the compiler-desugared doYielding:."
""
"Verifies that <coll> doYielding: [:elem | <body>] iterates over the"
"collection and binds each element to the block argument in order."
"Yieldability with `wait` inside the block is verified separately by the"
"actor test suite — see KNOWN ISSUE in"
"docs/superpowers/specs/2026-05-24-doyielding-design.md re: the"
"multi-worker race that surfaces when the block yields."

coll := OrderedCollection new.
coll add: 10. coll add: 20. coll add: 30.

"Iterate and sum."
sum := 0.
coll doYielding: [ :x | sum := sum + x ].
sum printNl.
