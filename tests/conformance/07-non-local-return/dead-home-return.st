"XFAIL: EXPECT: caught"
"§7.1 + D8: a ^ in a block whose home method already returned should signal a
catchable BlockCannotReturn exception. Spec-correct: an on: Error do: handler
catches it. Currently it is a hard runtime error that escapes the handler."
Object subclass: #Escaper.
Escaper >> escapedBlock
  ^ [ ^ 1 ].
e := Escaper new.
blk := e escapedBlock.
[ blk value. 'not reached' ]
  on: Error
  do: [ :ex | 'caught' ].
