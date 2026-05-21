"EXPECT: a Money worth 250"
"-- 04_mixins_and_uses.st --------------------------------------------------"
"-- Multiple inheritance / mixins via the `uses:` clause. `Money` is        --"
"-- assembled from two mixins: `Comparable` (ordering, expressed in terms   --"
"-- of `compareTo:`) and `Describable` (a printable description). A `Money` --"
"-- understands `>`, `<` and `describe` without redefining them.            --"
"--                                                                        --"
"-- Note (STATUS.md): a mixin's instance variables are read through an      --"
"-- accessor, not by bare name from the using class — `Money` reads its own --"
"-- `cents` directly, but reaches another Money's via `other cents`.        --"
"-- Expected output: a Money worth 250                                      --"

Object subclass: #Comparable.
Comparable >> > other
  ^ (self compareTo: other) > 0.
Comparable >> < other
  ^ (self compareTo: other) < 0.

Object subclass: #Describable.
Describable >> describe
  ^ 'a ' , self typeName , ' worth ' , self amount printString.

Object subclass: #Money
  instanceVariableNames: 'cents'
  uses: { Comparable. Describable }.

Money >> setCents: n
  cents := n.
  ^ self.
Money >> cents
  ^ cents.
Money >> amount
  ^ cents.
Money >> compareTo: other
  ^ cents - other cents.
Money >> typeName
  ^ 'Money'.

a := Money new setCents: 250.
b := Money new setCents: 100.
"a > b is true (250 > 100); describe comes from the Describable mixin."
(a > b) ifTrue: [ a describe ] ifFalse: [ b describe ].
