"EXPECT: #(true true 7 7 'constant ''owner'' of an Account read before it was set' 'constant ''owner'' of an Account read before it was set' 'Bea' 'constant ''owner'' of an Account read before it was set')"
"Spec §6.2 (the copy row) and §11, conformance 93: shallowCopy and deepCopy
 of a basicNew object whose constant owner is unset succeed; they move the
 attribute, unset included, without reading it as a value. Reading owner on
 either copy signals M14. The copy's owner is still unset, so reflection can
 fill it, and doing so leaves the original unset."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number
Account >> owner
  ^ owner

| a s d unset |
unset := [:block | block on: UnsetConstantError do: [:e | e messageText]].
a := Account basicNew.
a instVarNamed: #number put: 7.
s := [a shallowCopy] on: Error do: [:e | e messageText].
d := [a deepCopy] on: Error do: [:e | e messageText].
{s class == Account.
 d class == Account.
 s number.
 d number.
 unset value: [s owner].
 unset value: [d owner].
 d instVarNamed: #owner put: 'Bea'; owner.
 unset value: [a owner]}
