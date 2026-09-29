"EXPECT: #('a' 'b' 'c') #('a' 'b' 'c') #('a' 'b') an OrderedCollection('a' 'b' 'c') an OrderedCollection('a' 'b') an OrderedCollection('a' 'b' '' 'c') an OrderedCollection('a' 'b') an OrderedCollection('a' 'b') an OrderedCollection('a' 'b' 'c') an OrderedCollection('') #('a' '' 'b' 'c') #() #('x')"
"substrings / subStrings: / findTokens: drop empty tokens; splitOn: (Pharo 8+) keeps them and accepts a String, a Character or a Block; lines keeps empty lines and understands CR, LF and CRLF."
lf := String lf.
text := 'a' , lf , lf , 'b' , String cr , lf , 'c' , lf.
t1 := 'a b  c' substrings printString , ' ' , ('a,b,,c' subStrings: ',') printString , ' ' , 'a b' subStrings printString.
t2 := ('a,b;c' findTokens: ',;') printString , ' ' , ('a b' findTokens: $ ) printString.
t3 := ('a,b,,c' splitOn: ',') printString , ' ' , ('a--b' splitOn: '--') printString , ' ' , ('a,b' splitOn: $,) printString , ' ' , ('a1b2c' splitOn: [:ch | ch isDigit]) printString , ' ' , ('' splitOn: ',') printString.
t4 := text lines printString , ' ' , '' lines printString , ' ' , 'x' lines printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4.
