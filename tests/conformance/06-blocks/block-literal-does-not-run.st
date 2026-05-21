"EXPECT: 0"
"§6.1: evaluating a block literal produces the closure; it does not run the body"
n := 0.
[ n := 99 ].
n.
