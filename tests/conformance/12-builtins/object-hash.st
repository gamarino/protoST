"EXPECT: true"
"Equal values hash alike; distinct objects keep their identity hash."
a := Object new. b := Object new.
(3 hash = 3 hash) & ('abc' hash = ('ab' , 'c') hash) & (a hash = a hash) & (a identityHash ~= b identityHash) & (1 hash = 1.0 hash).
