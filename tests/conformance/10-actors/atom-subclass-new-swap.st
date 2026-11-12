"EXPECT: 3"
"§10.8 (S39): the same for a subclass instance made with new whose
initialize does not touch the cell."
Atom subclass: #Tally instanceVariableNames: 'label'.

Tally >> initialize
  label := 'hits'.

t := Tally new.
3 timesRepeat: [ t swap: [ :n | (n ifNil: [ 0 ]) + 1 ] ].
t value.
