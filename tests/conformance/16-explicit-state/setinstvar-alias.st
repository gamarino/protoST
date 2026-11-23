"EXPECT: #(true false 2 'an Account has no instance variable ''zork''' nil 'setInstVar:from:to: cannot write the constant ''number'' of an Account; assign it in initialize' 'setInstVar:from:to: cannot write the write-once variable ''oid'' of an Account; assign it with :=' true)"
"Spec §13.1, conformance 85 (0.17.0 only): setInstVar:from:to: is an alias of
 compareAndSet:from:to:, with its name check (M8: no attribute is created for
 an undeclared name, as 0.16 did) and its modifier checks (M5, under the
 alias's selector). It prints warning W1 on stderr (tests/cli)."
Object subclass: #Account instanceVariableNames: 'plain' constantNames: 'number' writeOnceNames: 'oid'.
Account >> initialize
  number := 1
Account >> plain
  ^ plain

| a r1 r2 r3 |
a := Account new.
r1 := [a setInstVar: #zork from: nil to: 1] on: Error do: [:e | e messageText].
r2 := [a setInstVar: #number from: 1 to: 2] on: ModificationForbidden do: [:e | e messageText].
r3 := [a setInstVar: #oid from: 3 to: 4] on: ModificationForbidden do: [:e | e messageText].
{a setInstVar: #plain from: nil to: 1.
 a setInstVar: #plain from: nil to: 9.
 (a setInstVar: #plain from: 1 to: 2) ifTrue: [a plain].
 r1.
 a instVarNamed: #zork.
 r2.
 r3.
 a setInstVar: #oid from: nil to: 4}
