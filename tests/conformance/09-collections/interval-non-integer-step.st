"EXPECT: #(1.0 1.5 2.0) 3 1.5 #(1 2) #((1/2) (3/2)) #(2.0 1.5 1.0) | 1 1.5 2.0 | 4.5 true #(0.5 1.5) #(1.5 2.0) true | 1 2 0.5 1.5 | 0"
"Intervals over Floats and Fractions: to:/to:by: with a non-integer bound or step, and the loops to:do: / to:by:do: over them (also when not inlined)."
iv := 1 to: 2 by: 0.5.
w := WriteStream on: String new.
1 to: 2 by: 0.5 do: [:x | w print: x; space].
w2 := WriteStream on: String new.
blk := [:x | w2 print: x; space].
1 to: 2.5 do: blk.
0.5 to: 2 do: blk.
t1 := iv asArray printString , ' ' , iv size printString , ' ' , (iv at: 2) printString , ' ' , (1 to: 2.5) asArray printString , ' ' , ((1/2) to: 2) asArray printString.
t2 := (2.0 to: 1 by: -0.5) asArray printString , ' | ' , w contents , '| ' , (iv inject: 0 into: [:a :b | a + b]) printString , ' ' , (iv includes: 1.5) printString.
t3 := (0.5 to: 2) asArray printString , ' ' , (iv select: [:x | x > 1]) printString , ' ' , (iv isKindOf: Interval) printString.
t1 , ' ' , t2 , ' ' , t3 , ' | ' , w2 contents , '| ' , (1 to: 0.5 by: 0.5) size printString.
