"EXPECT: 2"
"§9.2: reject: keeps elements for which the block is false"
(#(1 2 3 4) reject: [ :e | e > 2 ]) size.
