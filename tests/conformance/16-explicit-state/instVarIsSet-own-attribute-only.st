"EXPECT: #(false 'classSide' false false 'lateSide' false 'M14 owner' 'M14 owner' 'M14 owner' 'M14 owner' true true true)"
"Task 5's fix-round ruling and Task 9's dispatch: instVarIsSet: tests the
 object's OWN attribute, never the chain. A same-named class-instance
 variable that holds a value on the class side must not answer for an
 instance's unset constant: the probe answers false, in agreement with every
 read path (compiled read, instVarNamed:, instVarAt:, a read installed late),
 all of which signal UnsetConstantError. The class object's own probe
 answers for its class-instance variable."
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
a := Account basicNew.
late := LateAccount basicNew.
probe := [:block | block on: UnsetConstantError do: [:e | 'M14 ' , e variableName]].
Account setOwner: 'classSide'.
LateAccount setOwner: 'lateSide'.
{a instVarIsSet: #owner.
 Account owner.
 a instVarIsSet: 'owner'.
 late instVarIsSet: #owner.
 LateAccount owner.
 late instVarIsSet: 'owner'.
 probe value: [a owner].
 probe value: [a instVarNamed: #owner].
 probe value: [a instVarAt: 2].
 probe value: [late owner].
 Account instVarIsSet: #owner.
 (a instVarNamed: #owner put: 'ada'; instVarIsSet: #owner).
 a owner = 'ada'}
