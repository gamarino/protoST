"EXPECT-ERROR: unindented ^ after the method K"
"A return at column 1 after a method body is reported."
Object subclass: #K.
K >> f
  | x |
  x := 1.
^ x
