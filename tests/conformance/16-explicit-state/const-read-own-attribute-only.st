"EXPECT: #('M14 owner' 'M14 owner' 'M14 owner' 'M14 owner' 'classSide' 'M14 owner' 'lateSide')"
"Spec §6.2 and §6.9: a constant is the object's own attribute, unset while
 that attribute is absent or nil. A same-named class-instance variable that
 holds a value on the class side never answers for it: the compiled read,
 instVarAt:, instVarNamed: and a read installed late (a message-form class)
 all signal UnsetConstantError, as the class side keeps its own value."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account class instanceVariableNames: 'owner'.
Account class >> setOwner: x
  owner := x
Account class >> owner
  ^ owner
Account >> owner
  ^ owner
(Object yourself) subclass: #LateAccount
  instanceVariableNames: ''
  constantNames: 'owner'
  writeOnceNames: ''
  atomicNames: ''.
LateAccount class instanceVariableNames: 'owner'.
LateAccount class >> setOwner: x
  owner := x
LateAccount class >> owner
  ^ owner
LateAccount >> owner
  ^ owner

| a late probe |
Account setOwner: 'classSide'.
LateAccount setOwner: 'lateSide'.
a := Account basicNew.
late := LateAccount basicNew.
probe := [:block | block on: UnsetConstantError do: [:e | 'M14 ' , e variableName]].
{probe value: [a owner].
 probe value: [a instVarAt: 2].
 probe value: [a instVarNamed: 'owner'].
 probe value: [late owner].
 Account owner.
 probe value: [late instVarNamed: #owner].
 LateAccount owner}
