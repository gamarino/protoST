"EXPECT: #(false true true 'c' 'changed' 'c' 1 'R2E' 1)"
"D52, review round 2 (R2-L2): copying a class follows Pharo. The copy has a
 metaclass of its own, holding the original's class-side methods, whose
 soleInstance is the copy; redefining a class-side method of the copy leaves
 the original alone. Its instances still answer the instance side, and the
 class-instance variables are copied with the class side."
Object subclass: #R2E.
R2E class instanceVariableNames: 'n'.
R2E >> i  ^ 1.
R2E class >> c  ^ 'c'.
R2E class >> n  ^ n.
R2E class >> n: x  n := x.
R2E n: 1.
Smalltalk at: #R2Copy put: R2E copy.
R2Copy class >> c  ^ 'changed'.
{ R2Copy class == R2E class. R2Copy class soleInstance == R2Copy.
  R2E class soleInstance == R2E. R2E c. R2Copy c. R2E c.
  R2Copy new i. R2Copy name. R2Copy n }
