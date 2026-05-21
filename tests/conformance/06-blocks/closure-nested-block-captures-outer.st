"EXPECT: 11"
"§6.3: a block written inside another block sees the outer block's variables"
[ :x | [ :y | x + y ] value: 7 ] value: 4.
