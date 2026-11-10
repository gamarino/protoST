"EXPECT: 3 3 3 3"
"trimBoth and the String-argument branch of , keep a NUL; a Symbol receiver
 appends through the C++ path."
nul := String with: (Character value: 0).
padded := ' ' , 'a' , nul , 'b' , ' '.
trimmed := padded trimBoth.
joined := nul , 'ab'.
viaSymbol := #ab , nul.
trimmed size printString, ' ', joined size printString, ' ', viaSymbol size printString, ' ',
  (padded trimBoth) size printString
