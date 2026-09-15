"EXPECT: 30"
"§7 + D29: ^ inside a do: block nested in a to:do: body returns from the home"
"method, abandoning both loops."
Object subclass: #Finder.
Finder >> find
  1 to: 5 do: [ :i | #(1 2 3) do: [ :x | x = 3 ifTrue: [ ^ x * 10 ] ] ].
  ^ 0.
Finder new find.
