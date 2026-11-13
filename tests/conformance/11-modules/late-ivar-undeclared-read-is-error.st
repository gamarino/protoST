"EXPECT-ERROR: error: undeclared variable 'zork' in Kid (line 9)"
"§4.5 (S37): in a method of a class the compiler cannot see, a name that the
class does not declare is still an error -- signalled when the method is
installed, before it can run (here it is never sent)."
m := Import from: '_late_base_mod'.
LateBase subclass: #Kid.

Kid >> f
  ^ zork + 1.

3.
