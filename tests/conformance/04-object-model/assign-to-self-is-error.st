"EXPECT-ERROR: cannot assign to the pseudo-variable 'self'"
"self, super, thisContext, true, false and nil cannot be assigned."
Object subclass: #K.
K >> f
  self := 3.
  ^ self

K new f
