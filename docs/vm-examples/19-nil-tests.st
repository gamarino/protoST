Object subclass: #Cache instanceVariableNames: 'value'.

Cache >> at: aBlock
  ^ value ifNil: [ value := aBlock value ].

Cache >> describe
  ^ value ifNil: [ 'empty' ] ifNotNil: [ :v | v printString ].

| c |
c := Cache new.
(c at: [ 6 * 7 ]) printNl.
c describe printNl.
(c at: [ 0 ]) == 42 ifTrue: [ 'cached' printNl ].
