"and_or.st: 10^6 short-circuit conditionals with literal blocks, against the
 nested ifTrue: they mean (protoBaseST review P2). Every loop counts the
 iterations where its condition held and the program checks the counts, so
 a loop that failed or was skipped cannot look fast. Prints one line per
 form: name, iterations, hits, milliseconds; then 'and_or ok'."
Object subclass: #AndOrBench.

AndOrBench >> andLoop: n
  | hits |
  hits := 0.
  1 to: n do: [ :i | ((i > 10) and: [ i < 900000 ]) ifTrue: [ hits := hits + 1 ] ].
  ^ hits.

AndOrBench >> orLoop: n
  | hits |
  hits := 0.
  1 to: n do: [ :i | ((i < 10) or: [ i > 900000 ]) ifTrue: [ hits := hits + 1 ] ].
  ^ hits.

AndOrBench >> nestedLoop: n
  | hits |
  hits := 0.
  1 to: n do: [ :i | (i > 10) ifTrue: [ (i < 900000) ifTrue: [ hits := hits + 1 ] ] ].
  ^ hits.

| b n run |
b := AndOrBench new.
n := 1000000.
run := [ :name :expected :blk | | hits ms |
  ms := Time millisecondsToRun: [ hits := blk value ].
  hits = expected ifFalse: [ Error signal: name , ' answered ' , hits printString ].
  (name , ' ' , n printString , ' ' , hits printString , ' ' , ms printString , ' ms') displayNl ].
run value: 'method-and' value: 899989 value: [ b andLoop: n ].
run value: 'method-or' value: 100009 value: [ b orLoop: n ].
run value: 'method-nested-ifTrue' value: 899989 value: [ b nestedLoop: n ].
run value: 'script-and' value: 899989 value: [ | hits |
  hits := 0.
  1 to: n do: [ :i | ((i > 10) and: [ i < 900000 ]) ifTrue: [ hits := hits + 1 ] ].
  hits ].
run value: 'script-or' value: 100009 value: [ | hits |
  hits := 0.
  1 to: n do: [ :i | ((i < 10) or: [ i > 900000 ]) ifTrue: [ hits := hits + 1 ] ].
  hits ].
('and_or ok') displayNl.
