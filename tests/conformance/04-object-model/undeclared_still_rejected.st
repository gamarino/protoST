"EXPECT-ERROR: undeclared variable 'nosuch'"
"Knowing the kernel's instance variables does not open the door to typos."
IOStream subclass: #TypoStream.
TypoStream >> broken
  ^ nosuch + 1

TypoStream new broken.
