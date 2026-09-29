"EXPECT: #(#(2 4 6) #(2) 6 3 2 true false 'a Triple(1 2 3)')"
"A Collection subclass that defines only do: inherits the derived protocol
 (collect:, select:, inject:into:, size, detect:, includes:, isEmpty,
 printing), as in Smalltalk-80."
Collection subclass: #Triple instanceVariableNames: 'a b c'.
Triple class >> a: x b: y c: z  ^ self new setA: x b: y c: z.
Triple >> setA: x b: y c: z  a := x. b := y. c := z.
Triple >> do: aBlock  aBlock value: a; value: b; value: c.

| t |
t := Triple a: 1 b: 2 c: 3.
{ (t collect: [:x | x * 2]) asArray. (t select: [:x | x even]) asArray.
  t inject: 0 into: [:s :x | s + x]. t size. t detect: [:x | x > 1].
  (t includes: 3). t isEmpty. t printString }
