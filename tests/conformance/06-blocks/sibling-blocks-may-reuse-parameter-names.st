"EXPECT: #(2 4 6 5 7)"
"Sibling and sequential blocks may reuse parameter names; only nesting inside
 a scope that already binds the name is rejected."
Object subclass: #H.
H >> run
  | a b |
  a := #(1 2 3) collect: [:x | x * 2].
  b := (1 to: 3) inject: 0 into: [:acc :x | acc + x].
  ^ a , { b - 1. [:x | x + 1] value: 6 }

H new run
