"EXPECT: 5"
"§3.3: a method with no ^ returns self by default"
Object subclass: #Box
  instanceVariableNames: 'v'.
Box >> stash: n
  v := n.
Box >> v
  ^ v.
b := Box new.
(b stash: 5) v.
