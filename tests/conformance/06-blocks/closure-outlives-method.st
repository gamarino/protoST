"EXPECT: 9"
"§6.3: a block that outlives its creating method keeps captured variables alive"
Object subclass: #Maker.
Maker >> adderOf: n
  ^ [ :x | x + n ].
blk := Maker new adderOf: 4.
blk value: 5.
