"EXPECT: #(true true true true true 'owner' 'constant ''owner'' of an Account read before it was set' 'owner' 'constant ''owner'' of a LateAccount read before it was set')"
"Spec §6.2 and §6.9, conformance 97: every checking read path signals
 UnsetConstantError with the same receiver, variableName and messageText: a
 compiled read (an accessor), instVarAt: and instVarNamed:. A method
 installed late (a class built by the message form, resolved at install)
 gets the same checked read."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> owner
  ^ owner
(Object yourself) subclass: #LateAccount
  instanceVariableNames: ''
  constantNames: 'owner'
  writeOnceNames: ''
  atomicNames: ''.
LateAccount >> owner
  ^ owner

| acct late probe compiled at named lateRead |
acct := Account basicNew.
late := LateAccount basicNew.
probe := [:obj :block | block on: UnsetConstantError do: [:e |
  {e receiver == obj. e variableName. e messageText}]].
compiled := probe value: acct value: [acct owner].
at := probe value: acct value: [acct instVarAt: 2].
named := probe value: acct value: [acct instVarNamed: 'owner'].
lateRead := probe value: late value: [late owner].
{compiled = at. at = named. compiled first. lateRead first.
 (compiled at: 2) isString. compiled at: 2. compiled at: 3. lateRead at: 2. lateRead at: 3}
