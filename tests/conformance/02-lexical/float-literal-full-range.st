"EXPECT: 0 failed of 18 checked"
"§2.5 + D51 (K16): a float literal is the correctly rounded double (round to
 nearest, ties to even) over the whole range. Subnormals are literals: 5e-324
 is the smallest; a literal below half of it is 0.0 (-0.0 with a sign), as in
 IEEE 754, Pharo and Python. Each literal is checked by its bit pattern (the
 expected patterns are Python's struct.pack('>d', float(text)))."
| bits rows failed |
bits := [:x | ((WriteStream on: ByteArray new) nextPutFloat64: x bigEndian: true; contents) hex].
rows := {
  {5e-324. '0000000000000001'}. {-5e-324. '8000000000000001'}.
  {4.9406564584124654e-324. '0000000000000001'}. {1.0e-323. '0000000000000002'}.
  {2.4703282292062327e-324. '0000000000000000'}. {2.4703282292062328e-324. '0000000000000001'}.
  {1e-400. '0000000000000000'}. {-1e-400. '8000000000000000'}.
  {1.0e-320. '00000000000007e8'}. {2.225073858507201e-308. '000fffffffffffff'}.
  {2.2250738585072011e-308. '000fffffffffffff'}. {2.2250738585072014e-308. '0010000000000000'}.
  {1.7976931348623157e308. '7fefffffffffffff'}. {1.7976931348623158e308. '7fefffffffffffff'}.
  {0.1. '3fb999999999999a'}. {1.0e23. '44b52d02c7e14af6'}.
  {9007199254740993.0. '4340000000000000'}. {-0.0. '8000000000000000'} }.
failed := OrderedCollection new.
rows do: [:r | (bits value: r first) = r last ifFalse: [
  failed add: r first printString , ' is ' , (bits value: r first) , ', not ' , r last]].
failed isEmpty
  ifTrue: [failed size printString , ' failed of ' , rows size printString , ' checked']
  ifFalse: [failed asArray printString]
