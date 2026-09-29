"Kernel: Future combinators. whenAll: answers a Future of the Array of all
 results in order (rejected with the first rejection); whenAny: answers a
 Future of the first result. f1 & f2 and f1 | f2 are the two-future forms.

 They are built on thenDo: / catch: callbacks, which may run on different
 worker threads at once: the pending count and each result live in Atoms
 (compare-and-set), never in a shared Array, and resolve: / rejectWith: settle
 a Future only once."

Future class >> whenAll: aCollection
  | futures result slots remaining |
  futures := aCollection asArray.
  result := Future new.
  futures isEmpty ifTrue: [result resolve: #(). ^ result].
  slots := futures collect: [:each | Atom on: nil].
  remaining := Atom on: futures size.
  1 to: futures size do: [:i |
    (futures at: i) thenDo: [:value |
      (slots at: i) swap: [:old | value].
      (remaining swap: [:n | n - 1]) = 0
        ifTrue: [result resolve: (slots collect: [:slot | slot value])]].
    (futures at: i) catch: [:error | result rejectWith: error]].
  ^ result.

Future class >> whenAny: aCollection
  | result |
  result := Future new.
  aCollection do: [:each | each thenDo: [:value | result resolve: value]].
  ^ result.

Future >> & aFuture
  ^ Future whenAll: (Array with: self with: aFuture).

Future >> | aFuture
  ^ Future whenAny: (Array with: self with: aFuture).
