"-- used by late-method-modifier-rules.st: a method of a class the compiler
 cannot see that assigns a constant outside initialize (M1 at install)."
Acc >> setN: x
  n := x.
