"EXPECT: #(1 3 5 7 9) 9 true true #() true 0 #(1) #(-3 -2 -1) #(5 3 1) #(2 6 10 14 18) 25 true true #(9 7) 5 true"
"Intervals the native representation cannot hold (inexact stride, empty, single-element, LargeInteger bounds) keep the full Interval protocol; a zero step is an error."
a := 1 to: 10 by: 2.
n := 0.
(1 to: 0) do: [:x | n := n + 1].
big := (2 raisedTo: 70) to: (2 raisedTo: 70) + 4.
t1 := a asArray printString , ' ' , a last printString , ' ' , (a includes: 9) printString , ' ' , (a = #(1 3 5 7 9)) printString , ' ' , (1 to: 0) asArray printString , ' ' , (1 to: 0) isEmpty printString , ' ' , n printString.
t2 := (1 to: 1) asArray printString , ' ' , (-3 to: -1) asArray printString , ' ' , (5 to: 0 by: -2) asArray printString , ' ' , (a collect: [:x | x * 2]) printString , ' ' , (a inject: 0 into: [:s :x | s + x]) printString.
t3 := ((1 to: 0) isKindOf: Interval) printString , ' ' , (a isKindOf: Interval) printString , ' ' , (a reversed first: 2) printString , ' ' , big size printString , ' ' , ([1 to: 5 by: 0] on: Error do: [:e | true]) printString.
t1 , ' ' , t2 , ' ' , t3.
