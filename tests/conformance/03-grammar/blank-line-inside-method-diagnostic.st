"EXPECT-ERROR: blank line"
"A method temporary used after a blank line is reported with the rule that
 explains it: a blank line ends a method body."
Object subclass: #T.
T >> m
  | x |
  x := 1.

  ^ x.

T new m.
