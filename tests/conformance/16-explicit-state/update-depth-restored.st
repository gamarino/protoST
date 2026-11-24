"EXPECT: 7"
"Plan review focus 3, decision D-f: the update:with: depth that makes wait
 signal M7 returns to zero on every exit of the block: a normal answer, a
 nested update, a non-local return and an exception. A later wait on the
 same thread then works."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 10
Account >> leaveEarly
  ^ self update: #balance with: [:old | ^ 'left']
Account >> nested
  ^ self update: #balance with: [:old | (self update: #balance with: [:o | o]) + 1]
Account >> balance
  ^ balance

| a |
a := Account new.
a update: #balance with: [:old | old + 1].
a nested.
a leaveEarly.
[a update: #balance with: [:old | Error signal: 'boom']] on: Error do: [:e | nil].
[a update: #balance with: [:old | Future new wait]] on: Error do: [:e | nil].
(Future new resolve: 7; yourself) wait
