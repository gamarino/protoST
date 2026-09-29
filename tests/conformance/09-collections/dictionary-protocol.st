"EXPECT: 2 #bbbbbbbb 3 true nil"
"Dictionary add: an Association; keyAtValue: finds a key (nil if absent)."
d := Dictionary new.
d add: #aaaaaaaa -> 1; add: #bbbbbbbb -> 2.
d at: #cccccccc put: 3.
(d at: #bbbbbbbb) printString , ' ' , (d keyAtValue: 2) printString , ' ' , d size printString , ' ' , (d includesKey: #aaaaaaaa) printString , ' ' , (d keyAtValue: 99) printString.
