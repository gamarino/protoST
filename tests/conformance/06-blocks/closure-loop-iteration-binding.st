"XFAIL: EXPECT: 1 2 3"
"§6.3 + D30: a block argument belongs to one activation of its block, so the"
"blocks created in different to:do: iterations see different values of i."
"protoST keeps one captured dictionary per method (or module) activation, so"
"every stored block reads the last value: 3 3 3."
bs := OrderedCollection new.
1 to: 3 do: [ :i | bs add: [ i ] ].
(bs at: 1) value printString, ' ', (bs at: 2) value printString, ' ',
  (bs at: 3) value printString.
