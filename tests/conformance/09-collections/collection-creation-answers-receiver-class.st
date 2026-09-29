"XFAIL: EXPECT: true true true true true true"
"Class-side creators answer an instance of the RECEIVER, so a user subclass
 gets its own class back (Pharo: self new). XFAIL until the runtime fix lands:
 today new on a subclass of a built-in collection answers the base class.
 When this XPASSes, drop the XFAIL marker."
OrderedCollection subclass: #Stack.
Set subclass: #Tags.
Bag subclass: #Tally.
Dictionary subclass: #Registry.
Array subclass: #Row.
t1 := ((Stack with: 1 with: 2) class == Stack) printString , ' ' , ((Stack withAll: #(1 2)) class == Stack) printString , ' ' , ((Tags with: 1) class == Tags) printString.
t2 := ((Tally withAll: #(1 1)) class == Tally) printString , ' ' , ((Registry newFrom: {1->2}) class == Registry) printString , ' ' , ((Row with: 1 with: 2) class == Row) printString.
t1 , ' ' , t2.
