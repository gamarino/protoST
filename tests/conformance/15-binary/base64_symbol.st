"EXPECT: true"
"A Symbol is text: it decodes as its characters (as in Pharo, where a Symbol is a String)."
(#abcd base64Decoded = 'abcd' base64Decoded) printString.
