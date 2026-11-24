"EXPECT: wait inside an update:with: block: the block must be pure and may run several times"
"Spec §10.3, M7, conformance 73: wait inside an update:with: block signals
 M7, a pending Future and an already-settled one alike, and so does
 waitWithin:. The update is abandoned: balance keeps 10."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 10
Account >> balance
  ^ balance

| a r1 r2 r3 |
a := Account new.
r1 := [a update: #balance with: [:old | Future new wait]] on: Error do: [:e | e messageText].
r2 := [a update: #balance with: [:old | (Future new resolve: 1; yourself) wait]]
  on: Error do: [:e | e messageText].
r3 := [a update: #balance with: [:old | (Future new resolve: 1; yourself) waitWithin: 1000]]
  on: Error do: [:e | e messageText].
(r1 = r2 and: [r1 = r3 and: [a balance = 10]])
  ifTrue: [r1]
  ifFalse: [{r1. r2. r3. a balance} printString]
