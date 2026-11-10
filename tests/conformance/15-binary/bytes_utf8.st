"EXPECT: #[97 195 177 226 130 172] true 3 true 3 true 5 true"
"Strict UTF-8: round trip, NUL survives."
s := 'añ€'.
e := s utf8Encoded.
t := #[97 0 98] utf8Decoded.
te := t utf8Encoded.
e printString, ' ', (e utf8Decoded = s) printString, ' ', te size printString, ' ',
  (te utf8Decoded = t) printString, ' ', t size printString, ' ',
  (te = #[97 0 98]) printString, ' ', 'ab€' utf8Encoded size printString, ' ',
  ('' utf8Encoded isEmpty) printString.
