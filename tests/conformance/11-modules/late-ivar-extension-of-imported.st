"EXPECT: 84"
"§4.10 (S37): a method the importing program adds to an imported class reads
the class's instance variable."
m := Import from: '_late_base_mod'.

LateBase >> doubled
  ^ secret * 2.

b := LateBase new.
b init.
b doubled.
