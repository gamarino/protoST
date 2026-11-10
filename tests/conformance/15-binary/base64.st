"EXPECT: 0 failed of 21"
"Every vector of protoIO's base64.json, encoding and decoding."
| lib failed count |
lib := Import from: '_vectors_base64'.
failed := 0. count := 0.
lib VectorsBase64 all do: [:e |
  count := count + 1.
  (((e at: 1) base64Encoded = (e at: 2)) and: [(e at: 2) base64Decoded = (e at: 1)])
    ifFalse: [failed := failed + 1]].
failed printString, ' failed of ', count printString.
