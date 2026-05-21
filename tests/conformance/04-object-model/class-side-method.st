"EXPECT: 10"
"§4.7: a class-side method reachable from the class object (custom constructor)"
Object subclass: #Counter
  instanceVariableNames: 'value'.
Counter >> setValue: n
  value := n.
Counter >> value
  ^ value.
Counter class >> startingAt: n
  | c |
  c := self new.
  c setValue: n.
  ^ c.
(Counter startingAt: 10) value.
