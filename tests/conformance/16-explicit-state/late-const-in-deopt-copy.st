"EXPECT: 1"
"Plan review focus 1 and 2 together: initialize of a class the compiler
 cannot see assigns a constant in nested inlined blocks whose receivers are
 not Booleans. The de-optimised copy of the outer block inlines nothing, so
 the inner block is a real block inside it; it stands for code the source
 inlined, so install time resolves its store to the constant store, as the
 compiler does, instead of refusing it as a closure (M2)."
Object subclass: #Truthy.
Truthy >> ifTrue: aBlock
  ^ aBlock value

(Object yourself) subclass: #Acc instanceVariableNames: 'flag' constantNames: 'n' writeOnceNames: '' atomicNames: ''.
Acc >> initialize
  flag := Truthy new.
  flag ifTrue: [flag ifTrue: [n := 1]]

Acc new instVarNamed: #n
