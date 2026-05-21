"EXPECT: 8"
"§4.5: instance variable read and write inside methods"
Object subclass: #Box
  instanceVariableNames: 'v'.
Box >> set: n
  v := n.
Box >> get
  ^ v.
b := Box new.
b set: 8.
b get.
