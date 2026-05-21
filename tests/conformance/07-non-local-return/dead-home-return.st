"EXPECT: caught"
"§7.1 + D8 (closed, MNT-b2): a ^ in a block whose home method already returned
signals a catchable BlockCannotReturn (a subclass of Error), so an
`on: Error do:` handler catches it. It used to be a hard runtime error that
escaped the handler."
Object subclass: #Escaper.
Escaper >> escapedBlock
  ^ [ ^ 1 ].
e := Escaper new.
blk := e escapedBlock.
[ blk value. 'not reached' ]
  on: Error
  do: [ :ex | 'caught' ].
