"EXPECT-ERROR: undeclared"
"Assigning to an undeclared name inside a method is a compile error, not a
 silent new local (a typo would otherwise leave the real variable unchanged)."
Object subclass: #K instanceVariableNames: 'count'.
K >> bump
  cuont := count + 1.
  ^ self.

K new bump.
