"EXPECT: a absent 0 F absent 2"
"C6: element and key lookups that compare with `=` follow IEEE equality, so a"
"NaN equals no other object and is found only by identity. n and m are two"
"distinct NaNs. A Dictionary keyed by n answers its value for n and nothing"
"for m; a Bag holding only n counts no 1.5 and does not include 7; removing"
"2.5 from an OrderedCollection holding 1.5 and n finds nothing and keeps both"
"elements. (Set membership is protoCore's hashed membership, not `=`; see"
"docs/STATUS.md.)"
n := Float nan.
m := Float nan.
tf := [ :b | b ifTrue: [ 'T' ] ifFalse: [ 'F' ] ].
d := Dictionary new.
d at: n put: 'a'.
bag := Bag new.
bag add: n.
oc := OrderedCollection new.
oc add: 1.5.
oc add: n.
removed := oc remove: 2.5 ifAbsent: [ nil ].
(d at: n) , ' ' , (d at: m ifAbsent: [ 'absent' ]) , ' ' ,
  (bag occurrencesOf: 1.5) printString , ' ' , (tf value: (bag includes: 7)) , ' ' ,
  (removed isNil ifTrue: [ 'absent' ] ifFalse: [ 'removed' ]) , ' ' ,
  oc size printString.
