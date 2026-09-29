"EXPECT: heLLo hello $h ello ab 4 5 a SortedCollection($e $h $l $l $o) #($h $e $l $l $o) an OrderedCollection($h $i) heo true false"
"copyReplacing:with:, readStream, writeStream, asSet, asBag, asSortedCollection, asArray, asOrderedCollection, copyWithout:, noneSatisfy:."
s := 'hello'.
rs := s readStream.
c := rs next.
ws := s writeStream.
ws nextPutAll: 'ab'.
t1 := (s copyReplacing: $l with: $L) , ' ' , s , ' ' , c printString , ' ' , rs upToEnd , ' ' , ws contents.
t2 := s asSet size printString , ' ' , s asBag size printString , ' ' , s asSortedCollection printString , ' ' , s asArray printString , ' ' , 'hi' asOrderedCollection printString.
t3 := (s copyWithout: $l) , ' ' , (s noneSatisfy: [:ch | ch isDigit]) printString , ' ' , (s noneSatisfy: [:ch | ch isVowel]) printString.
t1 , ' ' , t2 , ' ' , t3.
