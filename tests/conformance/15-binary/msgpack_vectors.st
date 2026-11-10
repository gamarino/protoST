"EXPECT: 0 failed of 70 checked"
"Every entry of protoIO's msgpack.json (made by msgpack-python), in both directions. (1) decodeOrdered: gives the value tree that the entry stores. (2) Re-encoding the ordered form gives the stored bytes exactly. (3) The Smalltalk object built from the stored tree encodes to the stored bytes. Floats are compared by bit pattern, so a NaN payload and -0.0 count. A few entries have non-string map keys, so the strictKeys: false form decodes them; the default form must refuse them."
Object subclass: #MpCheck instanceVariableNames: ''.

MpCheck class >> hexOfFloat: aFloat
  | s |
  s := WriteStream on: ByteArray new.
  s nextPutFloat64: aFloat bigEndian: true.
  ^ s contents hex.

MpCheck class >> treeOf: o
  "The tagged tree of a decoded value, in the layout of the vector files."
  o isNil ifTrue: [^ #('nil')].
  (o == true or: [o == false]) ifTrue: [^ Array with: 'bool' with: o].
  o isInteger ifTrue: [^ Array with: 'int' with: o].
  o isFloat ifTrue: [^ Array with: 'f64' with: (self hexOfFloat: o)].
  (o isKindOf: ByteArray) ifTrue: [^ Array with: 'bin' with: o].
  (o isKindOf: String) ifTrue: [^ Array with: 'str' with: o].
  (o isKindOf: MessagePackExt) ifTrue: [^ Array with: 'ext' with: o type with: o data].
  (o isKindOf: MessagePackMap) ifTrue: [
    ^ Array with: 'map' with: (o associations collect: [:a |
        Array with: (self treeOf: a key) with: (self treeOf: a value)])].
  ^ Array with: 'array' with: (o collect: [:x | self treeOf: x]).

MpCheck class >> objectOf: t
  | tag |
  tag := t at: 1.
  tag = 'nil' ifTrue: [^ nil].
  (tag = 'bool' or: [tag = 'int' or: [tag = 'str' or: [tag = 'bin']]]) ifTrue: [^ t at: 2].
  tag = 'f64' ifTrue: [^ (ByteArray fromHex: (t at: 2)) float64At: 1 bigEndian: true].
  tag = 'ext' ifTrue: [^ MessagePackExt type: (t at: 2) data: (t at: 3)].
  tag = 'array' ifTrue: [^ (t at: 2) collect: [:x | self objectOf: x]].
  ^ MessagePackMap withAll: ((t at: 2) collect: [:p |
      (self objectOf: (p at: 1)) -> (self objectOf: (p at: 2))]).

lib := Import from: '_vectors_msgpack'.
failed := 0.
checked := 0.
strictRefused := 0.
lib VectorsMsgpack all do: [:e |
  | bytes tree decoded ok |
  bytes := e at: 2. tree := e at: 3.
  checked := checked + 1.
  decoded := MessagePack decodeOrdered: bytes strictKeys: false.
  ok := ((MpCheck treeOf: decoded) = tree)
    and: [(MessagePack encode: decoded) = bytes
      and: [(MessagePack encode: (MpCheck objectOf: tree)) = bytes]].
  ok ifFalse: [failed := failed + 1. (e at: 1) displayNl].
  [MessagePack decodeOrdered: bytes. nil] on: Error do: [:x | strictRefused := strictRefused + 1]].
strictRefused = 1 ifFalse: [failed := failed + 1].
failed printString, ' failed of ', checked printString, ' checked'.
