"-- used by late-method-modifier-rules.st: initialize of a class the compiler
 cannot see assigns a constant inside a real block (M2 at install)."
Acc >> initialize
  [n := 1] value.
