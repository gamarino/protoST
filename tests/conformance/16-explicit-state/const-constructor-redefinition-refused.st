"EXPECT-ERROR: number:owner: is the generated constructor of Account and cannot be redefined; give your constructor another name (line 7)"
"Spec §6.2, M9: the generated constructor cannot be redefined. The class is
 in the compiler's view, so the definition is a compile error."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> initialize
  ^ self
Account class >> number: n owner: o
  ^ nil

'not reached' printNl.
