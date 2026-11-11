"EXPECT: #(false false 2 2 true 3 #(1 2 3) true)"
"Each evaluation of a block literal makes a new closure object. Since 0.10.0
 a closure is built as an immutable object in one step; it keeps the identity
 semantics it had as a mutable one: two closures of the same literal in the
 same activation are neither == nor =, both stay members of a Set and keys of
 a Dictionary, a closure is equal to itself, each captures its own
 activation, and a closure survives in a collection after its activation."
| a b s d makers |
a := [ 1 ].
b := [ 1 ].
s := Set new. s add: a; add: b.
d := Dictionary new. d at: a put: 1; at: b put: 2.
makers := (1 to: 3) collect: [ :i | [ i ] ].
{ a == b. a = b. s size. d size. a = a. makers size.
  (makers collect: [ :m | m value ]) asArray.
  (s includes: a) and: [ s includes: b ] }
