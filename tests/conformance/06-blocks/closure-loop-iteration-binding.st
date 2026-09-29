"EXPECT: 1 2 3"
"§6.3: a block argument belongs to one activation of its block, so the blocks
 created in different to:do: iterations see different values of i (D30,
 closed in 0.4.0)."
bs := OrderedCollection new.
1 to: 3 do: [ :i | bs add: [ i ] ].
(bs at: 1) value printString, ' ', (bs at: 2) value printString, ' ',
  (bs at: 3) value printString.
