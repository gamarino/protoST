"Kernel: collection protocol written in protoST. The iteration primitives
 (do:, collect:, select:, inject:into:, detect:ifNone:, ...) and sort: are
 native; conversions, searching, sequencing and SortedCollection are here."

"--- any collection ----------------------------------------------------------"

Collection >> includes: anObject
  ^ self anySatisfy: [:each | each = anObject].

Collection >> occurrencesOf: anObject
  ^ self count: [:each | each = anObject].

Collection >> anyOne
  ^ self detect: [:each | true] ifNone: [self error: 'this collection is empty'].

Collection >> max
  ^ self inject: self anyOne into: [:m :each | m max: each].

Collection >> min
  ^ self inject: self anyOne into: [:m :each | m min: each].

Collection >> sum
  ^ self inject: 0 into: [:total :each | total + each].

Collection >> detectMax: aBlock
  | best bestValue |
  self do: [:each | | v |
    v := aBlock value: each.
    (bestValue isNil or: [v > bestValue]) ifTrue: [best := each. bestValue := v]].
  ^ best.

Collection >> detectMin: aBlock
  | best bestValue |
  self do: [:each | | v |
    v := aBlock value: each.
    (bestValue isNil or: [v < bestValue]) ifTrue: [best := each. bestValue := v]].
  ^ best.

Collection >> ifEmpty: aBlock
  ^ self isEmpty ifTrue: [aBlock value] ifFalse: [self].

Collection >> ifNotEmpty: aBlock
  ^ self isEmpty ifTrue: [self] ifFalse: [aBlock cull: self].

Collection >> ifEmpty: emptyBlock ifNotEmpty: notEmptyBlock
  ^ self isEmpty ifTrue: [emptyBlock value] ifFalse: [notEmptyBlock cull: self].

Collection >> isEmptyOrNil
  ^ self isEmpty.

Collection >> asOrderedCollection
  | out |
  out := OrderedCollection new.
  self do: [:each | out add: each].
  ^ out.

Collection >> asSet
  | out |
  out := Set new.
  self do: [:each | out add: each].
  ^ out.

Collection >> asBag
  | out |
  out := Bag new.
  self do: [:each | out add: each].
  ^ out.

Collection >> asSortedCollection
  ^ self asSortedCollection: [:a :b | a <= b].

Collection >> asSortedCollection: aBlock
  | out |
  out := SortedCollection sortBlock: aBlock.
  out addAll: self.
  ^ out.

Collection >> sorted
  ^ self asArray sort; yourself.

Collection >> sorted: aBlock
  ^ (self asArray sort: aBlock) yourself.

UndefinedObject >> isEmptyOrNil
  ^ true.

"--- sequenceable collections ------------------------------------------------"

SequenceableCollection >> first
  ^ self at: 1.

SequenceableCollection >> last
  ^ self at: self size.

SequenceableCollection >> second
  ^ self at: 2.

SequenceableCollection >> allButFirst
  ^ self copyFrom: 2 to: self size.

SequenceableCollection >> allButLast
  ^ self copyFrom: 1 to: self size - 1.

SequenceableCollection >> indexOf: anObject
  1 to: self size do: [:i | (self at: i) = anObject ifTrue: [^ i]].
  ^ 0.

SequenceableCollection >> copyFrom: start to: stop
  | out |
  out := OrderedCollection new.
  start to: stop do: [:i | out add: (self at: i)].
  ^ (self isKindOf: OrderedCollection) ifTrue: [out] ifFalse: [out asArray].

SequenceableCollection >> reversed
  | out |
  out := OrderedCollection new.
  self size to: 1 by: -1 do: [:i | out add: (self at: i)].
  ^ (self isKindOf: OrderedCollection) ifTrue: [out] ifFalse: [out asArray].

SequenceableCollection >> reverse
  ^ self reversed.

SequenceableCollection >> copyWith: anObject
  | out |
  out := self asOrderedCollection.
  out add: anObject.
  ^ (self isKindOf: OrderedCollection) ifTrue: [out] ifFalse: [out asArray].

SequenceableCollection >> withIndexDo: aBlock
  1 to: self size do: [:i | aBlock value: (self at: i) value: i].

SequenceableCollection >> doWithIndex: aBlock
  ^ self withIndexDo: aBlock.

SequenceableCollection >> keysAndValuesDo: aBlock
  1 to: self size do: [:i | aBlock value: i value: (self at: i)].

SequenceableCollection >> reverseDo: aBlock
  self size to: 1 by: -1 do: [:i | aBlock value: (self at: i)].

SequenceableCollection >> with: other do: aBlock
  other size = self size ifFalse: [^ self error: 'collections must have the same size'].
  1 to: self size do: [:i | aBlock value: (self at: i) value: (other at: i)].

SequenceableCollection >> with: other collect: aBlock
  | out |
  out := OrderedCollection new.
  self with: other do: [:a :b | out add: (aBlock value: a value: b)].
  ^ out asArray.

SequenceableCollection >> withIndexCollect: aBlock
  | out |
  out := OrderedCollection new.
  self withIndexDo: [:each :i | out add: (aBlock value: each value: i)].
  ^ out asArray.

"--- dictionaries -----------------------------------------------------------"

Dictionary >> add: anAssociation
  self at: anAssociation key put: anAssociation value.
  ^ anAssociation.

Dictionary >> keyAtValue: aValue
  ^ self keyAtValue: aValue ifAbsent: [nil].

Dictionary >> keyAtValue: aValue ifAbsent: aBlock
  self keysAndValuesDo: [:k :v | v = aValue ifTrue: [^ k]].
  ^ aBlock value.

"--- SortedCollection: an OrderedCollection that keeps its sort order ---------"

OrderedCollection subclass: #SortedCollection instanceVariableNames: 'sortBlock'.

SortedCollection class >> new
  ^ self sortBlock: [:a :b | a <= b].

SortedCollection class >> sortBlock: aBlock
  ^ self basicNew setSortBlock: aBlock.

SortedCollection >> setSortBlock: aBlock
  sortBlock := aBlock.
  ^ self.

SortedCollection >> sortBlock
  ^ sortBlock.

SortedCollection >> add: anObject
  super add: anObject.
  self sort: sortBlock.
  ^ anObject.

SortedCollection >> addAll: aCollection
  aCollection do: [:each | super add: each].
  self sort: sortBlock.
  ^ aCollection.

SortedCollection >> addFirst: anObject
  ^ self shouldNotImplement.

SortedCollection >> at: index put: anObject
  ^ self shouldNotImplement.
