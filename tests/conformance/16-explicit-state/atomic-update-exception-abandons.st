"EXPECT: #('boom' 10 'insufficient funds' 10)"
"Spec §6.4, conformance 72: an exception out of the update block abandons
 the update, and nothing is written."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 10
Account >> withdraw: n
  ^ self update: #balance with: [:old |
      old < n ifTrue: [InvalidArgument signal: 'insufficient funds'].
      old - n]
Account >> balance
  ^ balance

| a r1 r2 |
a := Account new.
r1 := [a update: #balance with: [:old | Error signal: 'boom'. 99]] on: Error do: [:e | e messageText].
r2 := [a withdraw: 50] on: InvalidArgument do: [:e | e messageText].
{r1. a balance. r2. a balance}
