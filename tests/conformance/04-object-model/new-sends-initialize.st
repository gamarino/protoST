"EXPECT: 1 1 raw"
"new sends initialize (0.4.0, closes D4); basicNew does not."
Object subclass: #C instanceVariableNames: 'n'.
C >> initialize
  n := 0.

C >> bump
  n := n + 1.
  ^ n.

C >> n
  ^ n.

C subclass: #D.
a := C new bump.
b := D new bump.
c := C basicNew n isNil ifTrue: ['raw'] ifFalse: ['initialized'].
a printString , ' ' , b printString , ' ' , c.
