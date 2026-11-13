"EXPECT: 84"
"§4.5, §11.2 (S37): a method of a subclass of an imported class reads the
instance variable the subclass inherits. The script is compiled before the
import runs, so the compiler cannot see LateBase; the name is resolved when
the method is installed."
m := Import from: '_late_base_mod'.
LateBase subclass: #Kid instanceVariableNames: 'own'.

Kid >> peek
  own := 2.
  ^ secret * own.

k := Kid new.
k init.
k peek.
