"EXPECT: true true true"
"protoIO ruling R7: a trailing group whose unused bits are not zero is refused
 ('abd=' and 'ab=='), while the canonical 'abc=' is accepted. CPython accepts all."
| refused |
refused := [:text | [text base64Decoded. false] on: Error do: [:e | true]].
('abc=' base64Decoded = #[105 183]) printString, ' ',
  (refused value: 'abd=') printString, ' ', (refused value: 'ab==') printString.
