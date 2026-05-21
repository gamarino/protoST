"EXPECT: true"
"§9.2: anySatisfy: is true if any element satisfies the block"
#(1 2 3) anySatisfy: [ :e | e > 2 ].
