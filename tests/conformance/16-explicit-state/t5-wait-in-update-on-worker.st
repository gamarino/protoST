"EXPECT: wait inside an update:with: block: the block must be pure and may run several times"
"Spec §14.3 T5, §10.3, M7: an actor's method runs update:with: whose block
 waits on a Future. The wait signals M7 on the worker before it can block or
 help, so it does not hang. The method catches it and answers the error
 text. Three waits: on a Future that another actor settles (the wait would
 help), on one nobody settles (it would block forever), and on an already
 settled one. The update is abandoned every time: balance keeps 10."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 10
Account >> balance
  ^ balance

Object subclass: #Echo instanceVariableNames: ''.
Echo >> echo: x
  ^ x

Object subclass: #Updater instanceVariableNames: ''.
Updater >> update: anAccount waitingOn: aBlock
  ^ [anAccount update: #balance with: [:old | aBlock value: old]]
      on: Error do: [:e |
        "M7 is a plain Error (spec §12), not one of its subclasses."
        e class == Error ifTrue: [e messageText] ifFalse: ['wrong class: ' , e class name]]

| acct echo updater r1 r2 r3 |
acct := Account new.
echo := Echo new asActor.
updater := Updater new asActor.
r1 := (updater update: acct waitingOn: [:old | (echo echo: old + 1) wait]) wait.
r2 := (updater update: acct waitingOn: [:old | Future new wait]) wait.
r3 := (updater update: acct waitingOn: [:old | (Future new resolve: old; yourself) wait]) wait.
(r1 = r2 and: [r1 = r3 and: [acct balance = 10]])
  ifTrue: [r1]
  ifFalse: [{r1. r2. r3. acct balance} printString]
