"EXPECT-ERROR: cannot assign to the argument 'x'"
"Method arguments are read-only, as in Smalltalk."
Object subclass: #K.
K >> f: x
  x := 5.
  ^ x

K new f: 1
