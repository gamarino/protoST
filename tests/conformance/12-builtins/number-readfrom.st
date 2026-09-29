"EXPECT: 42 3.5 -7 1000 250.0 0.01 42 12 ' apples' 2.5 '.' 7 3 true"
"Number readFrom: answers the number at the start of a String or ReadStream, in literal syntax (1e3 is the Integer 1000, as the literal is), leaving a stream just past it; Integer readFrom: works too; no digits is an error."
s := ReadStream on: '12 apples'.
n := Number readFrom: s.
r := ReadStream on: '2.5.'.
f := Number readFrom: r.
t1 := (Number readFrom: '42') printString , ' ' , (Number readFrom: '3.5') printString , ' ' , (Number readFrom: '-7') printString , ' ' , (Number readFrom: '1e3') printString , ' ' , (Number readFrom: '2.5e2') printString , ' ' , (Number readFrom: '1e-2') printString , ' ' , (Number readFrom: '42abc') printString.
t2 := n printString , ' ' , s upToEnd printString , ' ' , f printString , ' ' , r upToEnd printString , ' ' , (Integer readFrom: '7') printString , ' ' , (Number readFrom: '  3') printString.
t3 := ([Number readFrom: 'abc'. false] on: Error do: [:e | true]) printString.
t1 , ' ' , t2 , ' ' , t3.
