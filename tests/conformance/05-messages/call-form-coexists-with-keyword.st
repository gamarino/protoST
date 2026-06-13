"EXPECT: 510"
"a class may host BOTH a keyword method `foo:` AND a call-form `foo(x)`; "
"they are distinct selectors and dispatch independently."
Object subclass: #D instanceVariableNames: ''.
D >> double(x)  ^ x * 2.
D >> double: x  ^ x * 100.
D >> wrap(n)  ^ self double(n) + (self double: n).
D new wrap(5).
