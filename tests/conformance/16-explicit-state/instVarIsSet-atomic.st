"EXPECT: #(false true 0)"
"Spec §6.8, conformance 102: on an atomic variable instVarIsSet: answers
 false before initialize's compareAndSet:from:to: and true after it."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account >> initialize
  self compareAndSet: #balance from: nil to: 0
Account >> balance
  ^ balance

| a before |
a := Account basicNew.
before := a instVarIsSet: #balance.
a initialize.
{before. a instVarIsSet: #balance. a balance}
