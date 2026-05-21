"EXPECT: 2"
"§9.2: select: keeps elements for which the block is true"
(#(1 2 3 4) select: [ :e | e > 2 ]) size.
