"EXPECT: 101"
"§6.3 + D22: an instance variable is owned by `self`, never closure-captured.
A method that BOTH directly assigns an instance variable AND references it
from a nested block must still read/write the live instance variable — it
must not be boxed into an (uninitialised) closure dict. `balance` starts at
100; `balance := balance + 1` plus a block reading `balance` yields 101."
Object subclass: #Acct instanceVariableNames: 'balance'.
Acct >> initialize  balance := 100. ^ self.
Acct >> bump
  balance := balance + 1.
  [ balance ] value.
  ^ balance.
a := Acct new. a initialize.
a bump.
