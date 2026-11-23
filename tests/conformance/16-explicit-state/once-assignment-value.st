"EXPECT: #(true true true true true 'refused' true true 'refused')"
"Spec §6.3: the value of an assignment to a write-once variable is the value
 the variable holds afterwards, the argument when the write won and the equal
 winner's value when it lost silently, so `^ keySpec := aSpec` is
 protoBaseST's _publishKeySpec:. It holds for the assignment as an
 expression, inside a real block and in a statement whose value is
 discarded, and for a method resolved at install time (pre-flight rulings C2
 and C3: a class built by the message form)."
Object subclass: #KeyNode instanceVariableNames: '' writeOnceNames: 'keySpec'.
KeyNode >> _publishKeySpec: aSpec
  ^ keySpec := aSpec
KeyNode >> viaTemporary: aSpec
  | answer |
  answer := keySpec := aSpec.
  ^ answer
KeyNode >> inBlock: aSpec
  ^ [keySpec := aSpec] value
KeyNode >> discarding: aSpec
  keySpec := aSpec.
  ^ #done
(Object yourself) subclass: #LateKeyNode
  instanceVariableNames: ''
  constantNames: ''
  writeOnceNames: 'keySpec'
  atomicNames: ''.
LateKeyNode >> _publishKeySpec: aSpec
  ^ keySpec := aSpec

| k1 k2 n late results |
k1 := 'spec-' , 'alpha'.
k2 := 'spec' , '-alpha'.
(k1 == k2) ifTrue: [Error signal: 'k1 and k2 are identical; the test would prove nothing'].
n := KeyNode new.
late := LateKeyNode new.
results := OrderedCollection new.
results add: (n _publishKeySpec: k1) == k1.
results add: (n _publishKeySpec: k2) == k1.
results add: (n viaTemporary: k2) == k1.
results add: (n inBlock: k2) == k1.
results add: (n discarding: k2) == #done.
results add: ([n discarding: 'other'. 'not refused'] on: ModificationForbidden do: [:e | 'refused']).
results add: (late _publishKeySpec: k1) == k1.
results add: (late _publishKeySpec: k2) == k1.
results add: ([late _publishKeySpec: 'other'] on: ModificationForbidden do: [:e | 'refused']).
results asArray
