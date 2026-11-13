"nil_identity.st: 10^6 identity tests and nil tests in a method (protoBaseST
 review P4: `==` and ifNil:/ifNotNil: compiled inline). Every loop counts the
 iterations where its test held and the program checks the counts, so a loop
 that failed or was skipped cannot look fast. Prints one line per form: name,
 iterations, hits, milliseconds; then 'nil_identity ok'."
Object subclass: #NilBench instanceVariableNames: 'slots'.

NilBench >> setUp
  slots := Array new: 10.
  1 to: 10 by: 2 do: [ :i | slots at: i put: i ].

NilBench >> identityLoop: n
  | hits x |
  hits := 0. x := #a.
  1 to: n do: [ :i | x == #a ifTrue: [ hits := hits + 1 ] ].
  ^ hits.

NilBench >> ifNilLoop: n
  | hits |
  hits := 0.
  1 to: n do: [ :i | hits := hits + ((slots at: i \\ 10 + 1) ifNil: [ 0 ]) ].
  ^ hits.

NilBench >> ifNotNilLoop: n
  | hits |
  hits := 0.
  1 to: n do: [ :i | (slots at: i \\ 10 + 1) ifNotNil: [ :v | hits := hits + v ] ].
  ^ hits.

NilBench >> ifNilIfNotNilLoop: n
  | hits |
  hits := 0.
  1 to: n do: [ :i |
    hits := hits + ((slots at: i \\ 10 + 1) ifNil: [ 1 ] ifNotNil: [ :v | v ]) ].
  ^ hits.

| b n run |
b := NilBench new setUp; yourself.
n := 1000000.
run := [ :name :expected :blk | | hits ms |
  ms := Time millisecondsToRun: [ hits := blk value ].
  hits = expected ifFalse: [ Error signal: name , ' answered ' , hits printString ].
  (name , ' ' , n printString , ' ' , hits printString , ' ' , ms printString , ' ms') displayNl ].
run value: 'identity' value: 1000000 value: [ b identityLoop: n ].
run value: 'ifNil:' value: 2500000 value: [ b ifNilLoop: n ].
run value: 'ifNotNil:' value: 2500000 value: [ b ifNotNilLoop: n ].
run value: 'ifNil:ifNotNil:' value: 3000000 value: [ b ifNilIfNotNilLoop: n ].
('nil_identity ok') displayNl.
