"EXPECT: cba ABC 2 hellO 42 3.5 true 3 bc true abc 2 ABC eo true true 42"
"Strings are sequenceable collections of Characters (0.4.0)."
s := 'abc'.
b := [:x | x ifTrue: ['true'] ifFalse: ['false']].
r := OrderedCollection new.
s do: [:c | r add: c].
t1 := s reversed , ' ' , s asUppercase , ' ' , (s indexOf: $b) printString , ' ' , ('hello' copyReplaceAll: 'o' with: 'O').
t2 := '42' asNumber printString , ' ' , '3.5' asNumber printString , ' ' , (b value: ('abc' < 'abd')).
t3 := r size printString , ' ' , (s copyFrom: 2 to: 3) , ' ' , (b value: (s includesSubstring: 'bc')).
t4 := ('  abc  ' trimBoth) , ' ' , ('a b' substrings size) printString , ' ' , (s collect: [:c | c asUppercase]) , ' ' , ('hello' select: [:c | c isVowel]).
t5 := (b value: ('hello' beginsWith: 'he')) , ' ' , (b value: ('abc' asSymbol == #abc)) , ' ' , '42' asInteger printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4 , ' ' , t5.
