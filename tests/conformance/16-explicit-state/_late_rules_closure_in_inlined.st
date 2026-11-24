"-- used by late-method-modifier-rules.st: initialize of a class the compiler
 cannot see assigns a constant in a real block inside an inlined block (M2 at
 install: the inlined code itself holds the closure)."
Acc >> initialize
  h ifTrue: [[n := 1] value].
