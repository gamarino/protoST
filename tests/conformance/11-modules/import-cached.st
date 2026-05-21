"EXPECT: 5"
"§11.2: importing the same path twice yields the same (cached) module object —
the exported class is reachable through both module handles"
m1 := Import from: '_value_mod'.
m2 := Import from: '_value_mod'.
(m1 Holder compute) + 0.
