"EXPECT-ERROR: undeclared variable 'label' in K"
"A class-side method sees the class-instance variables and class variables,
 not the instances' variables (it runs with the class as self)."
Object subclass: #K instanceVariableNames: 'label'.
K class >> make  label := 'x'. ^ self new.

K make
