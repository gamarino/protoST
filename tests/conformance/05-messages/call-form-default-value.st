"EXPECT: 12"
"named default may reference an earlier positional parameter"
Object subclass: #C instanceVariableNames: ''.
C >> compose(n, factor = n + 1)  ^ n * factor.
C new compose(3).
