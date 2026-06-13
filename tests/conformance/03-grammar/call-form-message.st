"EXPECT: 50"
"call-form send: positional + named with default override"
Object subclass: #C instanceVariableNames: ''.
C >> mul(a, factor = 2)  ^ a * factor.
C new mul(5, factor = 10).
