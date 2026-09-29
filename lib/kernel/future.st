"Kernel: Future combinators. whenAll: answers a Future of the Array of all
 results in order (rejected with the first rejection); whenAny: answers a
 Future of the first result (rejected with the last rejection when every
 future is rejected, and at once for an empty collection). f1 & f2 and f1 | f2 are the two-future forms.

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
  | futures result remaining |
  futures := aCollection asArray.
  result := Future new.
  futures isEmpty ifTrue: [
    result rejectWith: (Error new messageText: 'whenAny: of an empty collection').
    ^ result].
  remaining := Atom on: futures size.
  futures do: [:each |
    each thenDo: [:value | result resolve: value].
    each catch: [:error |
      (remaining swap: [:n | n - 1]) = 0 ifTrue: [result rejectWith: error]]].
  ^ result.

Future >> & aFuture
  ^ Future whenAll: (Array with: self with: aFuture).

Future >> | aFuture
  ^ Future whenAny: (Array with: self with: aFuture).

"--- actor references ------------------------------------------------------------
 An actor proxy answers messages about the reference itself (identity,
 equality, nil tests, printing) synchronously; every other message is queued
 to the actor and answers a Future. These methods run on the proxy and never
 message it."

Object >> isActor
  ^ false

Actor >> isActor
  ^ true

Actor >> printString
  ^ self __wrappedObject __printStringForActor , ' (actor)'

Actor >> printOn: aStream
  aStream nextPutAll: self __wrappedObject __printStringForActor , ' (actor)'

Actor >> displayString
  ^ self __wrappedObject __printStringForActor , ' (actor)'

Actor >> printNl
  Transcript showCr: self __wrappedObject __printStringForActor , ' (actor)'

Actor >> displayNl
  Transcript showCr: self __wrappedObject __printStringForActor , ' (actor)'

"How an actor's object is shown in the actor's printString: its basic form
 ('a Thing'), or the value itself for immutable values."
Object >> __printStringForActor
  ^ self basicPrintString

Number >> __printStringForActor
  ^ self printString

String >> __printStringForActor
  ^ self printString
