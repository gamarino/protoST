Object subclass: #Finder.

Finder >> firstOver: limit in: aColl
  aColl do: [ :x | x > limit ifTrue: [ ^ x ] ].
  ^ nil.

(Finder new firstOver: 3 in: #(1 2 4 8)) printNl.
