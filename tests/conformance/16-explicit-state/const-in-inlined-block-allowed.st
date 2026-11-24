"EXPECT: 1"
"Spec §6.2, conformance 50 (twin): a block the compiler inlines in
 initialize is initialize's own code, so it may assign a constant."
Object subclass: #Account instanceVariableNames: 'history' constantNames: 'number'.
Account >> initialize
  true ifTrue: [number := 1]
Account >> number
  ^ number

Account new number
