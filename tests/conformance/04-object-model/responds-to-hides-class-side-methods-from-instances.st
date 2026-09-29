"EXPECT: #(true false true)"
"respondsTo: agrees with the send dispatch: a class-side method answers for
 the class, not for its instances."
Object subclass: #C.
C class >> make  ^ self new.
C >> work  ^ 1.

{ C respondsTo: #make. C new respondsTo: #make. C new respondsTo: #work }
