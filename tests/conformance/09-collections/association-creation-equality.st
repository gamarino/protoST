"EXPECT: 1->2 true true false false true 3->4 true"
"Association key:value: on both sides; = compares key and value; hash agrees with =."
a := Association key: 1 value: 2.
b := 1 -> 2.
c := 1 -> 3.
t1 := a printString , ' ' , (a = b) printString , ' ' , (a hash = b hash) printString , ' ' , (a = c) printString , ' ' , (a = 1) printString.
s := Set new.
s add: (1 -> 2); add: (1 -> 2).
t2 := (s size = 1) printString , ' ' , ((5 -> 6) key: 3 value: 4; yourself) printString , ' ' , ((#(1 2) -> 'x') = (#(1 2) -> 'x')) printString.
t1 , ' ' , t2.
