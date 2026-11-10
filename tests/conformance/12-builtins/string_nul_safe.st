"EXPECT: 1 3 3 3 5 3 3 3 3 98 1 3"
"A String may hold U+0000. Every String primitive builds its result from the
 full length of its text, so nothing after a NUL is lost."
nul := Character value: 0.
s := 'a' , (String with: nul) , 'b'.
a1 := nul asString size.
a2 := s size.
a3 := (s copyFrom: 1 to: 3) size.
a4 := s asUppercase size.
a5 := (s , 'xy') size.
a6 := s reversed size.
a7 := (s copyReplaceAll: 'a' with: 'c') size.
a8 := (s copyReplaceAll: 'zz' with: 'c') size.
a9 := (s , nul) size - 1.
a10 := (s at: 3) value.
a11 := (String with: nul) asLowercase size.
a12 := ((s , ' ' , s) substrings: ' ') first size.
a1 printString, ' ', a2 printString, ' ', a3 printString, ' ', a4 printString, ' ',
  a5 printString, ' ', a6 printString, ' ', a7 printString, ' ', a8 printString, ' ',
  a9 printString, ' ', a10 printString, ' ', a11 printString, ' ', a12 printString.
