"EXPECT: 3"
"§7: ^ inside a do: block that reads the enclosing to:do: loop variable"
"returns that variable's value from the home method."
Object subclass: #Finder.
Finder >> find
  1 to: 5 do: [ :i | #(3 4) do: [ :x | i = x ifTrue: [ ^ i ] ] ].
  ^ 0.
Finder new find.
