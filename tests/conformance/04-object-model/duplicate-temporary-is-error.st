"EXPECT-ERROR: is declared twice"
"A temporary or argument may be declared only once in a method or block."
Object subclass: #K.
K >> f
  | a a |
  ^ 1

K new f
