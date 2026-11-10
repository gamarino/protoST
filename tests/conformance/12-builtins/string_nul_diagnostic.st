"EXPECT: true true"
"An error text built from user data keeps a NUL: the selector of a failed
 perform: reaches messageText whole,."
nul := String with: (Character value: 0).
sel := ('a' , nul , 'b') asSymbol.
text := [3 perform: sel] on: Error do: [:e | e messageText].
mapped := text copyReplaceAll: nul with: 'X'.
(mapped includesSubstring: 'aXb') printString, ' ', (text size > 0) printString
