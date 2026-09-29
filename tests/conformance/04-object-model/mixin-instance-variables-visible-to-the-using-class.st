"EXPECT: #(5 5 6)"
"A class that uses a mixin sees the mixin's instance variables in its own
 methods; they are the same slots the mixin's methods use."
Object subclass: #M instanceVariableNames: 'x'.
M >> bumpX  x := x + 1.
Object subclass: #B uses: { M }.
B >> setX  x := 5.
B >> getX  ^ x.

| b |
b := B new setX; yourself.
{ b getX. b getX. (b bumpX; getX) }
