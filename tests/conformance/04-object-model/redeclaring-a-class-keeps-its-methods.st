"EXPECT: #(1 1 2 5)"
"Declaring a class again with the same superclass keeps its methods, its
 class variables and its instances, as in Pharo; a new instance variable may
 be added."
Object subclass: #A instanceVariableNames: 'x' classVariableNames: 'Count'.
A class >> count: n  Count := n.
A class >> count  ^ Count.
A >> one  ^ 1.

| old |
old := A new.
A count: 5.
Object subclass: #A instanceVariableNames: 'x y' classVariableNames: 'Count'.
A >> two  ^ 2.
{ A new one. old one. A new two. A count }
