"EXPECT: 13"
"call-form send binds at unary precedence — tighter than binary +"
Object subclass: #C instanceVariableNames: ''.
C >> twice(x)  ^ x * 2.
1 + (C new twice(6)).
