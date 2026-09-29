"EXPECT: 'ehllo' true 'ollhe' true false true 4 0 3 3 0 abc ABC true false true"
"String sorted answers a String; sameAs: is case-insensitive equality; indexOf:startingAt:, occurrencesOfString:, asLowercase/asUppercase and includesSubstring:caseSensitive:."
t1 := 'hello' sorted printString , ' ' , 'hello' sorted isString printString , ' ' , ('hello' sorted: [:a :b | a > b]) printString.
t2 := ('Hello' sameAs: 'hELLO') printString , ' ' , ('abc' sameAs: 'abd') printString , ' ' , (#Foo sameAs: 'foo') printString.
t3 := ('hello' indexOf: $l startingAt: 4) printString , ' ' , ('hello' indexOf: $l startingAt: 5) printString , ' ' , (#(1 2 1) indexOf: 1 startingAt: 2) printString.
t4 := ('abcabcab' occurrencesOfString: 'ab') printString , ' ' , ('abc' occurrencesOfString: 'x') printString , ' ' , 'aBc' asLowercase , ' ' , 'aBc' asUppercase.
t5 := ('Hello World' includesSubstring: 'world' caseSensitive: false) printString , ' ' , ('Hello World' includesSubstring: 'world' caseSensitive: true) printString , ' ' , ('Hello World' includesSubstring: 'World' caseSensitive: true) printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4 , ' ' , t5.
