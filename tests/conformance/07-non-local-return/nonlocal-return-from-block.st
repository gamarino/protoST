"EXPECT: 4"
"§7: ^ inside a block returns from the home method, abandoning the loop"
Object subclass: #Finder.
Finder >> firstOver: aColl
  aColl do: [ :x | (x > 3) ifTrue: [ ^ x ] ].
  ^ 0.
Finder new firstOver: #(1 2 4 8).
