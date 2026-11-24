"EXPECT: 1"
"Spec §6.2, conformance 60: a class without constants is unchanged: new
 runs initialize once and nothing else happens."
Object subclass: #Plain instanceVariableNames: 'x' classVariableNames: 'Count'.
Plain class >> count
  ^ Count
Plain >> initialize
  Count isNil ifTrue: [Count := 0].
  Count := Count + 1.
  x := Count

Plain new.
Plain count
