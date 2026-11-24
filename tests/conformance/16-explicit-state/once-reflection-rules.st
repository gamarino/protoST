"EXPECT: #(nil nil 'alpha-key' false 'instVarNamed:put: cannot replace the write-once variable ''oid'' of a PBAtom; nothing was written' 'instVarNamed:put: cannot replace the write-once variable ''oid'' of a PBAtom; nothing was written' 'instVarNamed:put: cannot replace the write-once variable ''oid'' of a PBAtom; nothing was written' 'instVarNamed:put: cannot replace the write-once variable ''oid'' of a PBAtom; nothing was written' true)"
"Spec §6.5 and §11, M6, conformance 67: instVarNamed:put: fills a write-once
 variable only while it is unset (nil writes nothing). Once set, every
 reflective write signals ModificationForbidden (M6), even with an equal
 value: reflection is not a publication protocol, unlike :=. instVarAt:put:
 reaches the same rule. The variable keeps its first value."
Object subclass: #PBAtom instanceVariableNames: '' writeOnceNames: 'oid'.
PBAtom >> oid
  ^ oid

| a k1 k2 refused r |
refused := [:block | block on: ModificationForbidden do: [:e | e messageText]].
k1 := 'alpha-' , 'key'.
k2 := 'alpha' , '-key'.
a := PBAtom basicNew.
r := OrderedCollection new.
r add: (a instVarNamed: #oid put: nil).
r add: a oid.
r add: (a instVarNamed: #oid put: k1).
r add: k1 == k2.
r add: (refused value: [a instVarNamed: #oid put: k2]).
r add: (refused value: [a instVarNamed: #oid put: 'beta-key']).
r add: (refused value: [a instVarAt: 1 put: k1]).
r add: (refused value: [a instVarNamed: #oid put: nil]).
r add: a oid == k1.
r asArray
