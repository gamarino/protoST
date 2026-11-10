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

"--- deadlines -------------------------------------------------------------------
 Measured on the monotonic clock, in milliseconds (a non-negative Integer). The
 runtime's timer thread only keeps the deadlines: when one passes it hands the
 settle to the worker pool, so callbacks (thenDo:, catch:) run on a worker,
 never on the timer thread, and a slow callback delays no other timer. Timers
 still pending when the program ends are dropped; they never keep it running.
 Periodic and delayed work compose from these, for example
 (Future after: 500) thenDo: [:x | anActor tick]."

"A Future resolved with nil once ms milliseconds have passed."
Future class >> after: ms
  ^ Future new __resolveNilAfter: ms.

"A new Future that settles as the receiver does if that happens within ms
 milliseconds, and is otherwise rejected with a TimeoutError. The receiver is
 not cancelled: it may still settle later, and that result is then ignored by
 the answer (a Future settles once). An already settled receiver gives an
 answer settled the same way at once."
Future >> within: ms
  | result |
  result := Future new.
  self thenDo: [:value | result resolve: value].
  self catch: [:error | result rejectWith: error].
  ^ result __rejectTimeoutAfter: ms.

"(self within: ms) wait: the receiver's value, or a TimeoutError signalled when
 ms milliseconds pass first. Inside an actor method it suspends the actor, as
 wait does; elsewhere it blocks the calling thread."
Future >> waitWithin: ms
  ^ (self within: ms) wait.

"Sent on a worker by the timer service when a within: deadline passes before
 the receiver settled (src/primitives/timer_prims.cpp). Private."
Future >> __timedOutAfter: ms
  self rejectWith: (TimeoutError new
    messageText: 'the Future did not settle within ' , ms printString , ' ms').

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
