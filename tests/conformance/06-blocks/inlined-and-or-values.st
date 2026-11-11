"EXPECT: #(true false false false true true true false 3 nil false true 7 nil false #('a' 'b') 2 #(5 3) true)"
"and: / or: with a literal block are compiled inline (0.10.0) and answer what
 the Boolean primitives answer: the truth tables; the block's value as it is,
 not converted to a Boolean (true and: [3] is 3); nil for an empty block; the
 receiver alone when the result is decided, with the block NOT evaluated; a
 block that assigns a temporary and nests another and:/or:; a real block
 created inside the inlined body captures by reference; the same inside a
 method."
Object subclass: #AO.
AO >> run: x
  | s b |
  s := 0.
  (x > 1 and: [ b := [ s ]. true ]) ifTrue: [ s := 5 ].
  ^ { b value. x }
AO >> both: a and: b
  ^ (a > 0 and: [ b > 0 ]) or: [ a = b ]
| log t |
log := OrderedCollection new.
t := { true and: [ true ]. true and: [ false ]. false and: [ true ]. false and: [ false ].
       true or: [ true ]. true or: [ false ]. false or: [ true ]. false or: [ false ].
       true and: [ 3 ]. true and: [ ]. false and: [ log add: #notRun. 1 ]. true or: [ log add: #notRun. 1 ].
       false or: [ | u | 7 ] }.
t := t , { log isEmpty ifTrue: [ nil ] ifFalse: [ log asArray ] }.
t := t , { (true and: [ log add: #a. true ]) and: [ (false or: [ log add: #b. false ]) or: [ false ] ]. log asArray }.
t := t , { (true and: [ | n | n := 1. n := n + 1. n ]) }.
t := t , { AO new run: 3 }.
t := t , { (AO new both: 0 and: 0) and: [ (AO new both: 1 and: 2) and: [ (AO new both: -1 and: 2) not ] ] }.
t
