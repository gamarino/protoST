"EXPECT: 80000"
"Spec §14.3 T1: 8 actors each send deposit: 1 10,000 times to one shared
 Account, passed to them directly (not as an actor). deposit: uses
 update:with: on an atomic variable, so no increment is lost: the balance is
 exactly 80,000. Every actor reports the deposits it made, and the program
 checks that total too, so the printed figure is the work actually done.
 Checked by count only."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 0
Account >> deposit: amount
  ^ self update: #balance with: [:old | old + amount]
Account >> balance
  ^ balance

Object subclass: #Depositor instanceVariableNames: ''.
Depositor >> deposit: amount into: anAccount times: n
  | made |
  made := 0.
  1 to: n do: [:i | anAccount deposit: amount. made := made + 1].
  ^ made

| acct depositors futures made |
acct := Account new.
depositors := (1 to: 8) collect: [:i | Depositor new asActor].
futures := depositors collect: [:d | d deposit: 1 into: acct times: 10000].
made := 0.
futures do: [:f | made := made + f wait].
(made = 80000 and: [acct balance = made])
  ifTrue: [acct balance]
  ifFalse: [{made. acct balance} printString]
