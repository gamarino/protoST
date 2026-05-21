"EXPECT: false"
"§9.2: allSatisfy: is true only if every element satisfies the block"
#(1 2 3) allSatisfy: [ :e | e > 2 ].
