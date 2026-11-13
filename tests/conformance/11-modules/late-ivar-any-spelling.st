"EXPECT: 4 5 9 3"
"S37, review M-4: a method of a class the compiler does not know resolves
 instance variables of any spelling (`_x`, `Y`) when it is installed, and a
 capitalised name that is not a variable is still a global (`Smalltalk`)."
(Object yourself) subclass: #K instanceVariableNames: '_x Y'.
K >> set
  _x := 4.
  Y := 5.
K >> get
  ^ _x printString, ' ', Y printString, ' ', (_x + Y) printString, ' ', (Array with: 1 with: 2 with: 3) size printString.
(K new set; yourself) get
