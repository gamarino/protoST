"EXPECT: 50"
"§7 + D22: the guard-clause style — an early `^ ivar` inside an `ifTrue:`
block followed by the method's main body — compiles correctly. The guard is
not taken here (50 <= 100), the body subtracts, and the trailing `^ balance`
returns the live instance variable: 100 - 50 = 50."
Object subclass: #Acct instanceVariableNames: 'balance'.
Acct >> initialize  balance := 100. ^ self.
Acct >> withdraw: amount
  amount > balance ifTrue: [ ^ balance ].
  balance := balance - amount.
  ^ balance.
a := Acct new. a initialize.
a withdraw: 50.
