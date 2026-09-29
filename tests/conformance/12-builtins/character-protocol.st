"EXPECT: Character 97 $a true B b true false true a true"
"Characters are their own class ($a, 'abc' at: 1, 97 asCharacter)."
b := [:x | x ifTrue: ['true'] ifFalse: ['false']].
c := $a.
(c class name) , ' ' , c value printString , ' ' , c printString , ' ' , (b value: c isVowel) , ' ' , $b asUppercase asString , ' ' , $B asLowercase asString , ' ' , (b value: $5 isDigit) , ' ' , (b value: $a isUppercase) , ' ' , (b value: ('abc' at: 1) = $a) , ' ' , 97 asCharacter asString , ' ' , (b value: $a < $b).
