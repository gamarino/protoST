"EXPECT: #('A one' 'A two')"
"§4.6: every message of a cascade whose receiver is super is a super send."
Object subclass: #A instanceVariableNames: 'log'.
A >> initialize
  log := OrderedCollection new.
A >> log
  ^ log.
A >> one
  log add: 'A one'.
A >> two
  log add: 'A two'.
A subclass: #B.
B >> one
  log add: 'B one'.
B >> two
  log add: 'B two'.
B >> test
  super one; two.
  ^ log asArray.
B new test
