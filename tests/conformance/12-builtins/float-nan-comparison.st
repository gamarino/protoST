"EXPECT: FFFFFTFTFTFFFFFF 1 1 F"
"C6: comparisons involving NaN follow IEEE 754. `Float nan` answers a new NaN"
"object on every send. With n and m two NaNs: n < 1, n <= 1, n > 1, n >= 1,"
"n = 1 are false and n ~= 1 true; n = n is false and n ~= n true; n = m is"
"false; identity n == n stays true; 1 < n, 1 = n, n = 1.5, n = 2**70,"
"2**70 <= n and n = 0 are false. min: and max: answer the argument because"
"no ordering holds, and between:and: is false."
n := Float nan.
m := Float nan.
big := 2 raisedTo: 70.
tf := [ :b | b ifTrue: [ 'T' ] ifFalse: [ 'F' ] ].
r := ''.
{ n < 1. n <= 1. n > 1. n >= 1. n = 1. n ~= 1. n = n. n ~= n. n = m. n == n.
  1 < n. 1 = n. n = 1.5. n = big. big <= n. n = 0 }
    do: [ :b | r := r , (tf value: b) ].
r , ' ' , (n min: 1) printString , ' ' , (n max: 1) printString , ' ' ,
  (tf value: (n between: 0 and: 2)).
