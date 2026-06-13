"EXPECT: 107"
"bare call inside a method is a self-send"
Object subclass: #E instanceVariableNames: ''.
E >> base()  ^ 100.
E >> total(n)  ^ n + base().
E new total(7).
