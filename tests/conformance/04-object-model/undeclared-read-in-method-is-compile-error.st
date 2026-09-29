"EXPECT-ERROR: undeclared variable 'zork' in C"
"Reading an undeclared lowercase name inside a method is reported when the
 script is compiled, not when the method first runs."
Object subclass: #C.
C >> f  ^ zork + 1.

3
