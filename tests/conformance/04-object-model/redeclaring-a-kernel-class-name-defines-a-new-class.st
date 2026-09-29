"EXPECT: #(true 5)"
"Declaring a class with the name of a kernel class defines a new class; it
 does not merge into the kernel's (Point keeps no kernel methods)."
Object subclass: #Point instanceVariableNames: 'r theta'.
Point >> r  ^ 5.
Point >> r: x  r := x.

{ (Point new respondsTo: #x) not. (Point new r: 5; yourself) r }
