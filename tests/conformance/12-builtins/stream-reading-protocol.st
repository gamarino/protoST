"EXPECT: $h hello false world true nil nil ab $d $c true false $e true $b #(1 2) #(3) #(5) an OrderedCollection(1) 0"
"ReadStream over Strings, Arrays and OrderedCollections: skipSeparators peek upTo: upToEnd atEnd next (nil past the end, as in Pharo) next: skip: peekFor: skipTo:."
rs := ReadStream on: '  hello world'.
rs skipSeparators.
p := rs peek.
h := rs upTo: $ .
t1 := p printString , ' ' , h , ' ' , rs atEnd printString , ' ' , rs upToEnd , ' ' , rs atEnd printString , ' ' , rs next printString , ' ' , rs peek printString.
r2 := 'abcdef' readStream.
a := r2 next: 2.
r2 skip: 1.
d := r2 next.
r2 skip: -2.
c := r2 next.
t2 := a , ' ' , d printString , ' ' , c printString , ' ' , (r2 peekFor: $d) printString , ' ' , (r2 peekFor: $z) printString , ' ' , r2 next printString.
r3 := 'a,b' readStream.
t3 := (r3 skipTo: $,) printString , ' ' , r3 next printString.
ra := ReadStream on: #(1 2 3 4 5).
t4 := (ra next: 2) printString , ' ' , (ra upTo: 4) printString , ' ' , ra upToEnd printString , ' ' , ((ReadStream on: (OrderedCollection withAll: #(1 2 3))) upTo: 2) printString , ' ' , (ReadStream on: #()) upToEnd size printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4.
