"EXPECT: #(5 12 12 nil 1)"
"Spec §6.4, conformance 69: update:with: answers the value it installed. On
 an unset atomic variable the block receives nil."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 0
Account >> deposit: n
  ^ self update: #balance with: [:old | old + n]
Account >> balance
  ^ balance

| a b seen |
a := Account new.
b := Account basicNew.
{a deposit: 5.
 a deposit: 7.
 a balance.
 (b update: #balance with: [:old | seen := old. 1]) == 1 ifTrue: [seen].
 b balance}
