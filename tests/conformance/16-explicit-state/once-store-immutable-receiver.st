"EXPECT: #('ModificationForbidden' 'cannot assign to the write-once variable ''oid'' of a BlockClosure: the receiver is immutable; nothing was written' nil)"
"Spec §12 (Task 3/4 ruling): the text of a write-once store
 (STORE_ONCE_INSTVAR) whose receiver is an immutable value. A write-once
 assignment compiles in a real block, so a block made in a method of PBAtom,
 whose argument stands for the method's self slot, is installed as a
 BlockClosure method with the private __installMethod:as: and sent to a
 block: self is then that block, an immutable value (the route of
 store-immutable-receiver-is-modification-forbidden.st). The constant store's
 twin cannot be reached from Smalltalk: a constant is assigned only in
 initialize and its inlined blocks, whose de-optimised copies take no
 argument, so none can run as a method. A unit case pins that text."
Object subclass: #PBAtom instanceVariableNames: '' writeOnceNames: 'oid'.
PBAtom >> grab
  ^ [:ignored | oid := 1]

| target |
BlockClosure __installMethod: PBAtom new grab as: #setOid.
target := [:a | a].
([target setOid] on: ModificationForbidden do: [:e | {e class name. e messageText}])
  , {target instVarNamed: 'oid'}
