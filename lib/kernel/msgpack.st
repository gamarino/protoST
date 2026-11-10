"Kernel: MessagePack, bound to protoIO's codec, with MessagePackMap and MessagePackExt.

 The bytes are those of msgpack-python 1.x as ProtoBase uses it (packb with
 use_bin_type=True, unpackb with raw=False). Encoding walks the object graph in
 C++ and decoding builds objects from protoIO's reader events, so there is no
 per-element bytecode and no recursion.

 Smalltalk to MessagePack, and back:
   nil, true, false                 nil, bool                    same
   Integer, -2^63 .. 2^64 - 1       the smallest int / uint      Integer
   an Integer outside that range    signals Error (never wraps)
   Float                            float64                      Float (a float32 widens)
   String, Symbol                   str (UTF-8, NUL kept)        String
   ByteArray                        bin                          ByteArray
   Array, OrderedCollection         array                        Array
   Dictionary                       map, in no specified order   Dictionary
   MessagePackMap                   map, in the order it holds   MessagePackMap (decodeOrdered: only)
   MessagePackExt                   ext                          MessagePackExt
   anything else                    signals Error 'MessagePack cannot encode an instance of <Class>'
 A Symbol decodes as a String: a Symbol of up to six bytes already is a String
 here (D35), so no other rule could tell #foo from #foobarbaz.

 A cyclic structure signals Error ('MessagePack: cyclic structure'), and so
 does one nested deeper than 512 levels, the depth the decoder accepts. A
 container met twice without being inside itself is shared, not cyclic.

 Map keys are strict by default, as in msgpack-python: only str and bin keys
 decode. decode:strictKeys: false accepts any key. A Dictionary has no order
 (protoCore's attribute order is unspecified), so encoding one writes its
 entries in an unspecified order and decoding one drops the wire order; use
 decodeOrdered: and MessagePackMap when the order of the bytes matters."

Object subclass: #MessagePack instanceVariableNames: ''.

MessagePack class >> encode: anObject
  "The MessagePack bytes of anObject, a ByteArray."
  ^ self __msgpackEncode: anObject map: MessagePackMap ext: MessagePackExt.

MessagePack class >> decode: aByteArray
  "The object aByteArray holds. Bytes after it signal Error; map keys must be str or bin."
  ^ self decode: aByteArray strictKeys: true.

MessagePack class >> decode: aByteArray strictKeys: aBoolean
  "As decode:; with false a map may have keys of any type."
  ^ self __msgpackDecode: aByteArray strict: aBoolean ordered: false
      map: MessagePackMap ext: MessagePackExt first: false.

MessagePack class >> decodeFirst: aByteArray
  "An Association of the first object in aByteArray and the number of bytes it took; bytes after it are allowed."
  ^ self __msgpackDecode: aByteArray strict: true ordered: false
      map: MessagePackMap ext: MessagePackExt first: true.

MessagePack class >> decodeOrdered: aByteArray
  "As decode:, but each map becomes a MessagePackMap holding its Associations in wire order."
  ^ self decodeOrdered: aByteArray strictKeys: true.

MessagePack class >> decodeOrdered: aByteArray strictKeys: aBoolean
  "As decodeOrdered:; with false a map may have keys of any type."
  ^ self __msgpackDecode: aByteArray strict: aBoolean ordered: true
      map: MessagePackMap ext: MessagePackExt first: false.

Object subclass: #MessagePackMap instanceVariableNames: 'associations'.

MessagePackMap class >> withAll: anArrayOfAssociations
  "A value that encodes as a map with exactly these entries, in this order."
  anArrayOfAssociations isArray
    ifFalse: [^ self error: 'MessagePackMap withAll: an Array of Associations was expected'].
  anArrayOfAssociations do: [:each |
    (each isKindOf: Association)
      ifFalse: [^ self error: 'MessagePackMap withAll: needs Associations, not ' , each printString]].
  ^ self __fromAssociations: anArrayOfAssociations.

MessagePackMap class >> __fromAssociations: anArray
  "Unchecked: the decoder builds the Array itself."
  ^ self basicNew __setAssociations: anArray.

MessagePackMap >> __setAssociations: anArray
  associations := anArray.
  ^ self.

MessagePackMap >> associations
  "The Associations, in wire order."
  ^ associations.

MessagePackMap >> size
  ^ associations size.

MessagePackMap >> at: aKey ifAbsent: aBlock
  "The value of the LAST entry whose key = aKey: a map with a repeated key keeps
   the last one, as msgpack-python's unpackb and decode: do. (The entries
   themselves all stay in associations, in wire order.)"
  associations size to: 1 by: -1 do: [:i |
    | each |
    each := associations at: i.
    each key = aKey ifTrue: [^ each value]].
  ^ aBlock value.

MessagePackMap >> = other
  ^ (other isKindOf: MessagePackMap) and: [associations = other associations].

MessagePackMap >> hash
  "The size and the keys of the first three entries: equal maps (= compares the
   Associations in order) have equal keys there, so equal hashes."
  | h |
  h := associations size.
  1 to: (associations size min: 3) do: [:i |
    h := (h * 31 + (associations at: i) key hash) \\ 1073741824].
  ^ h.

MessagePackMap >> printOn: aStream
  aStream nextPutAll: 'a MessagePackMap'; print: associations.

Object subclass: #MessagePackExt instanceVariableNames: 'type data'.

MessagePackExt class >> type: anInteger data: aByteArray
  "An application extension: anInteger is its type, from -128 to 127 (msgpack reserves -1 for timestamps); aByteArray its payload."
  (anInteger isInteger and: [anInteger >= -128 and: [anInteger <= 127]])
    ifFalse: [^ self error: 'MessagePackExt: the ext type must be an Integer from -128 to 127'].
  (aByteArray isKindOf: ByteArray)
    ifFalse: [^ self error: 'MessagePackExt: a ByteArray was expected for the data'].
  ^ self basicNew __setType: anInteger data: aByteArray.

MessagePackExt >> __setType: anInteger data: aByteArray
  type := anInteger.
  data := aByteArray.
  ^ self.

MessagePackExt >> type
  ^ type.

MessagePackExt >> data
  ^ data.

MessagePackExt >> = other
  ^ (other isKindOf: MessagePackExt) and: [type = other type and: [data = other data]].

MessagePackExt >> hash
  ^ type hash bitXor: data hash.

MessagePackExt >> printOn: aStream
  aStream nextPutAll: 'a MessagePackExt type: '; print: type; nextPutAll: ' data: '; print: data.
