"EXPECT: 0 failed of 20 checked"
"§2.5 + D51 (K16): printString of a finite Float is a float literal that
 reads back as the same Float, bit for bit, at the edges of the range
 (subnormals, the normal/subnormal boundary, the largest Float, -0.0, one
 ulp above 1.0). Each row is a bit pattern, the literal printString writes
 for it, written as source, and the same text as a String: the Float built
 from the bits must print as that text, the literal must have those bits,
 and asNumber must read the text back to them."
| bits rows failed |
bits := [:x | ((WriteStream on: ByteArray new) nextPutFloat64: x bigEndian: true; contents) hex].
rows := {
  {'0000000000000001'. 5.0e-324. '5.0e-324'}.
  {'0000000000000002'. 1.0e-323. '1.0e-323'}.
  {'00000000000007e8'. 1.0e-320. '1.0e-320'}.
  {'000fffffffffffff'. 2.225073858507201e-308. '2.225073858507201e-308'}.
  {'0010000000000000'. 2.2250738585072014e-308. '2.2250738585072014e-308'}.
  {'0010000000000001'. 2.225073858507202e-308. '2.225073858507202e-308'}.
  {'7fefffffffffffff'. 1.7976931348623157e308. '1.7976931348623157e308'}.
  {'7fe0000000000000'. 8.98846567431158e307. '8.98846567431158e307'}.
  {'8000000000000001'. -5.0e-324. '-5.0e-324'}.
  {'800fffffffffffff'. -2.225073858507201e-308. '-2.225073858507201e-308'}.
  {'3fb999999999999a'. 0.1. '0.1'}.
  {'3ff0000000000001'. 1.0000000000000002. '1.0000000000000002'}.
  {'4340000000000001'. 9007199254740994.0. '9007199254740994.0'}.
  {'3cb0000000000000'. 2.220446049250313e-16. '2.220446049250313e-16'}.
  {'44b52d02c7e14af6'. 1.0e23. '1.0e23'}.
  {'54b249ad2594c37d'. 1.0e100. '1.0e100'}.
  {'0000000000000000'. 0.0. '0.0'}.
  {'8000000000000000'. -0.0. '-0.0'}.
  {'3e7ad7f29abcaf48'. 1.0e-7. '1.0e-7'}.
  {'c0934a456d5cfaad'. -1234.5678. '-1234.5678'}. }.
failed := OrderedCollection new.
rows do: [:r | | x |
  x := (ByteArray fromHex: (r at: 1)) float64At: 1 bigEndian: true.
  x printString = (r at: 3) ifFalse: [failed add: (r at: 1) , ' prints ' , x printString].
  (bits value: (r at: 2)) = (r at: 1) ifFalse: [failed add: (r at: 3) , ' as a literal is ' , (bits value: (r at: 2))].
  (bits value: (r at: 3) asNumber) = (r at: 1) ifFalse: [failed add: (r at: 3) , ' asNumber is ' , (bits value: (r at: 3) asNumber)]].
failed isEmpty
  ifTrue: [failed size printString , ' failed of ' , rows size printString , ' checked']
  ifFalse: [failed asArray printString]
