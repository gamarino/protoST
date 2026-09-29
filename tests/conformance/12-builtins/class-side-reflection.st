"EXPECT: #(true false true #('make') #('work') true true)"
"Reflection on the class side: a metaclass understands its class-side
 methods and the class protocol, lists its class-side selectors, and its
 superclass is the superclass's metaclass."
Object subclass: #A.
A class >> make  ^ self new.
A >> work  ^ 1.
A subclass: #B.

{ A class canUnderstand: #make. A class canUnderstand: #work. A class canUnderstand: #new.
  A class selectors asArray. A selectors asArray.
  B class superclass == A class. A new respondsTo: #work }
