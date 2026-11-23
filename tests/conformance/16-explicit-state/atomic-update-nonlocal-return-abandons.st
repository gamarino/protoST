"EXPECT: #('refused' 10 15)"
"Spec §6.4, conformance 71: a non-local return out of the update block
 abandons the update, and nothing is written; a later update works."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 10
Account >> withdraw: n
  ^ self update: #balance with: [:old |
      old < n ifTrue: [^ 'refused'].
      old - n]
Account >> deposit: n
  ^ self update: #balance with: [:old | old + n]
Account >> balance
  ^ balance

| a r |
a := Account new.
r := a withdraw: 50.
{r. a balance. a deposit: 5}
