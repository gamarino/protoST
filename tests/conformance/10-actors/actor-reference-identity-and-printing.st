"EXPECT: #(true false true true false 'a Thing (actor)' 'a Thing (actor)' true 5)"
"An actor proxy answers messages about the reference synchronously:
 identity, equality (so collections find it), nil tests and printing (as
 'a Thing (actor)'). Other messages still go to the actor and answer Futures."
Object subclass: #Thing.
Thing >> five  ^ 5.

| a b |
a := Thing new asActor.
b := Thing new asActor.
{ a == a. a == b. (OrderedCollection with: a) includes: a. a notNil. a isNil.
  a printString. (Array with: a) first printString. (a ifNil: [false] ifNotNil: [:x | x == a]).
  a five wait }
