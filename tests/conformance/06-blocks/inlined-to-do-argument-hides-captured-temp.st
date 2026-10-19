"EXPECT: #(10 6 10)"
"An inlined to:do: whose argument reuses the name of a captured method
 temporary binds its own variable: the body sums the loop variable, and the
 method's i, captured by a block, keeps its value."
Object subclass: #F.
F >> test
  | i blk sum |
  i := 10.
  blk := [i].
  sum := 0.
  1 to: 3 do: [:i | sum := sum + i].
  ^ { blk value. sum. i }

F new test
