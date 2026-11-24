"EXPECT: the constant 'number' of an Account is already set; nothing was written"
"Spec §6.2, M3, conformance 51: a constant is set at most once. A second,
 explicit initialize finds it set; the store signals ModificationForbidden in
 place, so the handler catches it by class (pre-flight ruling C1)."
Object subclass: #Account instanceVariableNames: 'history' constantNames: 'number'.
Account >> initialize
  number := 7

| a |
a := Account new.
[a initialize. 'no error'] on: ModificationForbidden do: [:e | e messageText]
