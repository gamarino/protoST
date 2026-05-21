"EXPECT: 6"
"§3.1: top-level forms execute in source order; last statement's value wins"
a := 1.
a := a + 2.
a := a + 3.
a.
