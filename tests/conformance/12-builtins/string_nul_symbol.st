"EXPECT: 3 true 3 true 98 true"
"A Symbol may hold U+0000: asSymbol interns by content (not through a C
 string), two equal strings give the identical Symbol, and Symbol asString
 answers the full text. A computed selector goes through the same path."
n := #[97 0 98] utf8Decoded.
s := n asSymbol.
t := ('a' , (String with: (Character value: 0)) , 'b') asSymbol.
sz := s asString size.
same := s == t.
plain := ('ab' , 'c') asSymbol == #abc.
sz printString, ' ', same printString, ' ', s size printString, ' ',
  plain printString, ' ', (s asString at: 3) value printString, ' ',
  (s asString = n) printString
