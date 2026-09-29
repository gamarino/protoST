"EXPECT: a-b-c 1 2 3 true x=3; hello world"
"WriteStream / ReadStream need no import; on a String they build a String."
w := WriteStream on: String new.
#('a' 'b' 'c') do: [:e | w nextPutAll: e] separatedBy: [w nextPut: $-].
r := ReadStream on: #(1 2 3).
out := OrderedCollection new.
[r atEnd] whileFalse: [out add: r next].
w2 := WriteStream on: String new.
w2 nextPutAll: 'x='; print: 3; nextPut: $;.
t := (ReadStream on: 'hello world') upTo: $ .
w contents , ' ' , (out at: 1) printString , ' ' , (out at: 2) printString , ' ' , (out at: 3) printString , ' ' , (r atEnd ifTrue: ['true'] ifFalse: ['false']) , ' ' , w2 contents , ' ' , t , ' ' , (String streamContents: [:s | s nextPutAll: 'world']).
