"EXPECT: 7"
"§6.3: a block written at module top level captures module-level variables"
acc := 0.
[ acc := acc + 7 ] value.
acc.
