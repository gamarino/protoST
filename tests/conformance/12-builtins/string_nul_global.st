"EXPECT: true"
"The diagnostic for an undefined global keeps a NUL in the name."
nul := String with: (Character value: 0).
text := [Smalltalk at: ('a' , nul , 'b') asSymbol] on: Error do: [:e | e messageText].
((text copyReplaceAll: nul with: 'X') includesSubstring: 'aXb') printString
