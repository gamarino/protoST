"EXPECT: #('A foo with B bar' 'A foo with B bar and B bar' 'A + B bar')"
"§4.6: super affects only the send whose receiver it is. A send in the
 arguments of a super send (here self bar) is an ordinary send: bar runs the
 override, and foo: runs the inherited method."
Object subclass: #A.
A >> foo: x
  ^ 'A foo with ', x.
A >> foo: x and: y
  ^ 'A foo with ', x, ' and ', y.
A >> + x
  ^ 'A + ', x.
A >> bar
  ^ 'A bar'.
A subclass: #B.
B >> foo: x
  ^ 'B foo with ', x.
B >> foo: x and: y
  ^ 'B foo with ', x, ' and ', y.
B >> + x
  ^ 'B + ', x.
B >> bar
  ^ 'B bar'.
B >> test1
  ^ super foo: self bar.
B >> test2
  ^ super foo: self bar and: (self bar).
B >> test3
  ^ super + self bar.
{ B new test1. B new test2. B new test3 }
