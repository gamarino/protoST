"EXPECT: 1"
"§6.3: a block local declared with | x | is a distinct variable; writing it does
not disturb the enclosing method's same-named temporary. (D6 warns that the flat
per-method captured dictionary cannot distinguish the two; in this arrangement
the method's x is nonetheless observed unchanged.)"
Object subclass: #Shadow.
Shadow >> run
  | x |
  x := 1.
  [ :y | | x | x := 999. y ] value: 0.
  ^ x.
Shadow new run.
