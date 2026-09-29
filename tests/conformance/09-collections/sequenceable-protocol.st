"EXPECT: 1 3 true 2 #(3 2 1) #(2 3) 3 6 #(1 2 3) #(3 2 1) #(1 1 2 3) 1-a2-b"
"Everyday sequenceable protocol and sorting (0.4.0 kernel)."
a := #(1 2 3).
s := WriteStream on: String new.
#($a $b) withIndexDo: [:c :i | s print: i; nextPut: $-; nextPut: c].
t1 := a first printString , ' ' , a last printString , ' ' , (a includes: 2) printString , ' ' , (a indexOf: 2) printString.
t2 := a reversed printString , ' ' , (a copyFrom: 2 to: 3) printString , ' ' , a max printString , ' ' , a sum printString.
t3 := #(3 1 2) asSortedCollection asArray printString , ' ' , (#(1 3 2) asSortedCollection: [:x :y | x > y]) asArray printString , ' ' , (#(3 1 2 1) asOrderedCollection sort; yourself) asArray printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , s contents.
