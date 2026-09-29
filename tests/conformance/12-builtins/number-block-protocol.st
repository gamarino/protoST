"EXPECT: 3 FF 1010 16rFF true 2 3 6 true false 007 true 30"
"Everyday protocol of numbers and blocks (0.4.0 kernel)."
n := 0. 3 timesRepeat: [n := n + 1].
a := n printString.
b := 255 printString: 16.
c := 10 radix: 2.
d := 255 printStringRadix: 16.
e := (3 between: 1 and: 5) printString.
f := [:x :y | x + y] numArgs printString.
g := ([:x :y | x + y] valueWithArguments: #(1 2)) printString.
h := (#(1 2 3) inject: 0 into: [:s :x | s + x]) printString.
i := 7 isPrime printString.
j := 8 isPrime printString.
k := 7 printPaddedWith: $0 to: 3.
l := ([:x | x * 2] cull: 3) = 6.
m := (1 to: 4) inject: 0 into: [:s :x | s + (x * x)].
a , ' ' , b , ' ' , c , ' ' , d , ' ' , e , ' ' , f , ' ' , g , ' ' , h , ' ' , i , ' ' , j , ' ' , k , ' ' , l printString , ' ' , m printString.
