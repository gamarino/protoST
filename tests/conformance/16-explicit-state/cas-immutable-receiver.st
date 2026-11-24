"EXPECT: #('ModificationForbidden' 'compareAndSet:from:to: cannot write the instance variable ''v'' of a BlockClosure: the receiver is immutable; nothing was written' 'ModificationForbidden' 'compareAndSet:from:to: cannot write the instance variable ''v'' of a BlockClosure: the receiver is immutable; nothing was written' 'ModificationForbidden' 'update:with: cannot write the instance variable ''v'' of a BlockClosure: the receiver is immutable; nothing was written' 'ModificationForbidden' 'setInstVar:from:to: cannot write the instance variable ''v'' of a BlockClosure: the receiver is immutable; nothing was written' nil)"
"Spec §2, §6.5 (review 1, I1 and I3): a CAS message on an immutable receiver
 signals ModificationForbidden and never answers false, whether or not the
 expected value matches. A block is an immutable value; declaring 'v' on
 BlockClosure gives it a declared variable, so the name check passes and the
 write itself is refused. Nothing is written."
BlockClosure instanceVariableNames: 'v'.

| target refuse |
target := [:x | x].
refuse := [:action | action on: ModificationForbidden do: [:e | {e class name. e messageText}]].
(refuse value: [target compareAndSet: #v from: nil to: 6])
  , (refuse value: [target compareAndSet: #v from: 5 to: 6])
  , (refuse value: [target update: #v with: [:old | 1]])
  , (refuse value: [target setInstVar: #v from: 5 to: 6])
  , {target instVarNamed: #v}
