"EXPECT: 4"
"§7 + D29: ^ directly in a to:do: body that also evaluates a block literal"
"returns from the method."
Object subclass: #Finder.
Finder >> find
  1 to: 5 do: [ :i | [ 1 ] value. i = 4 ifTrue: [ ^ i ] ].
  ^ 0.
Finder new find.
