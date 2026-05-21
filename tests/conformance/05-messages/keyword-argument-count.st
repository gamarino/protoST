"EXPECT: 60"
"§5.4: a keyword send carries one argument per keyword part"
Object subclass: #Adder.
Adder >> sum: a and: b and: c
  ^ a + b + c.
Adder new sum: 10 and: 20 and: 30.
