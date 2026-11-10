"EXPECT: 42 1 1 true true true"
"ByteArray = and hash go by contents: two equal values built separately are one key. new: answers zero bytes, and a NUL byte survives concatenation."
a := ByteArray withAll: #(1 0 2).
b := #[1] , #[0 2].
d := Dictionary new.
d at: a put: 42.
d at: b put: 42.
s := Set new.
s add: a; add: b.
c := #[0] , #[0 7 0].
(d at: (ByteArray withAll: #(1 0 2))) printString , ' ' , d size printString , ' ' , s size printString , ' ' ,
  ((ByteArray new: 3) = #[0 0 0]) printString , ' ' ,
  (a hash = b hash) printString , ' ' ,
  (c size = 4 and: [(c at: 1) = 0 and: [(c at: 3) = 7 and: [c = #[0 0 7 0]]]]) printString.
