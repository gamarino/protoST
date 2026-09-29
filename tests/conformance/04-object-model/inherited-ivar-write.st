"EXPECT: 100"
"A subclass method reads and writes an instance variable its superclass declares."
Object subclass: #Account instanceVariableNames: 'balance'.
Account >> balance
  ^ balance.

Account subclass: #Savings instanceVariableNames: 'rate'.
Savings >> setBalance: n
  balance := n.
  ^ self.

(Savings new setBalance: 100) balance.
