"EXPECT: true true true true true true true"
"Overlong forms, surrogates, truncation and out-of-range values are all refused."
bad := [:bytes | [bytes utf8Decoded. false] on: Error do: [:e | true]].
(bad value: #[192 175]) printString, ' ', (bad value: #[237 160 128]) printString, ' ',
  (bad value: #[226 130]) printString, ' ', (bad value: #[244 144 128 128]) printString, ' ',
  (bad value: #[128]) printString, ' ', (bad value: #[240 128 128 128]) printString, ' ',
  (bad value: #[97 226 130]) printString.
