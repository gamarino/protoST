"EXPECT: true true true true true abc"
"Strings are immutable: at:put: signals ModificationForbidden (an Error) whose text says so and points at WriteStream and copyReplaceAll:with:."
s := 'abc'.
e := [s at: 1 put: $x. nil] on: Error do: [:ex | ex].
t := e messageText.
(e isKindOf: ModificationForbidden) printString , ' ' , (e isKindOf: Error) printString , ' ' , (t includesSubstring: 'immutable') printString , ' ' , (t includesSubstring: 'WriteStream') printString , ' ' , (t includesSubstring: 'copyReplaceAll:with:') printString , ' ' , s.
