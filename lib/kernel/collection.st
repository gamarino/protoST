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

Collection >> species
  "The class that derived collections are built from. As in Pharo it is the
   receiver's own class; an Interval answers Array (see below)."
  ^ self class.

Collection >> addAll: aCollection
  aCollection do: [:each | self add: each].
  ^ aCollection.

Collection >> removeAll: aCollection
  "Remove each element of aCollection (an error if one is absent) and answer
   aCollection."
  (aCollection == self ifTrue: [aCollection asArray] ifFalse: [aCollection])
    do: [:each | self remove: each].
  ^ aCollection.

Collection >> removeAll
  self asArray do: [:each | self remove: each].

Collection >> removeAllSuchThat: aBlock
  (self select: aBlock) asArray do: [:each | self remove: each].

Collection >> includesAll: aCollection
  ^ aCollection allSatisfy: [:each | self includes: each].

Collection >> includesAny: aCollection
  ^ aCollection anySatisfy: [:each | self includes: each].

Collection >> noneSatisfy: aBlock
  ^ (self anySatisfy: aBlock) not.

Collection >> copyWith: newElement
  ^ self copy add: newElement; yourself.

Collection >> copyWithout: oldElement
  ^ self reject: [:each | each = oldElement].

Collection >> isArray
  ^ false.

Collection >> groupedBy: aBlock
  "Answer a Dictionary mapping each value of aBlock to the collection (of the
   receiver's species) of the elements that produced it."
  | groups species |
  groups := Dictionary new.
  self do: [:each |
    (groups at: (aBlock value: each) ifAbsentPut: [OrderedCollection new]) add: each].
  species := self species.
  species == OrderedCollection ifFalse: [
    groups keys do: [:key | groups at: key put: (species withAll: (groups at: key))]].
  ^ groups.

Collection >> detectSum: aBlock
  ^ self inject: 0 into: [:total :each | total + (aBlock value: each)].

Collection >> sum: aBlock
  "Like sum, answers 0 for an empty collection. Otherwise the running total
   starts from the first value, so summands need not be addable to 0."
  | total first |
  first := true.
  self do: [:each |
    first
      ifTrue: [total := aBlock value: each. first := false]
      ifFalse: [total := total + (aBlock value: each)]].
  ^ first ifTrue: [0] ifFalse: [total].

Collection >> average
  self isEmpty ifTrue: [^ self error: 'this collection is empty'].
  ^ self sum / self size.

Object >> isArray
  ^ false.

Array >> isArray
  ^ true.

"--- instance creation ---------------------------------------------------------
 Class-side creators answer an instance of the RECEIVER (self new / self new:),
 so a user subclass gets its own class back."

Collection class >> new: anInteger
  "Growable and hashed collections need no preallocation: the size is a hint."
  ^ self new.

Collection class >> with: anObject
  ^ self new add: anObject; yourself.

Collection class >> with: first with: second
  ^ self new add: first; add: second; yourself.

Collection class >> with: first with: second with: third
  ^ self new add: first; add: second; add: third; yourself.

Collection class >> with: first with: second with: third with: fourth
  ^ self new add: first; add: second; add: third; add: fourth; yourself.

Collection class >> with: first with: second with: third with: fourth with: fifth
  ^ self new add: first; add: second; add: third; add: fourth; add: fifth; yourself.

Collection class >> with: first with: second with: third with: fourth with: fifth with: sixth
  ^ self new add: first; add: second; add: third; add: fourth; add: fifth; add: sixth; yourself.

Collection class >> withAll: aCollection
  ^ self new addAll: aCollection; yourself.

Collection class >> newFrom: aCollection
  ^ self withAll: aCollection.

OrderedCollection class >> withAll: aCollection
  ^ self new addAll: aCollection; yourself.

Set class >> withAll: aCollection
  ^ self new addAll: aCollection; yourself.

Bag class >> withAll: aCollection
  ^ self new addAll: aCollection; yourself.

"An Array has a fixed size and no add:, so its creators fill a new: instance."

Array class >> new: size withAll: anObject
  | array |
  array := self new: size.
  1 to: size do: [:i | array at: i put: anObject].
  ^ array.

Array class >> withAll: aCollection
  | array i |
  array := self new: aCollection size.
  i := 0.
  aCollection do: [:each | array at: (i := i + 1) put: each].
  ^ array.

Array class >> with: anObject
  ^ (self new: 1) at: 1 put: anObject; yourself.

Array class >> with: first with: second
  ^ (self new: 2) at: 1 put: first; at: 2 put: second; yourself.

Array class >> with: first with: second with: third
  ^ (self new: 3) at: 1 put: first; at: 2 put: second; at: 3 put: third; yourself.

Array class >> with: first with: second with: third with: fourth
  ^ (self new: 4) at: 1 put: first; at: 2 put: second; at: 3 put: third; at: 4 put: fourth; yourself.

Array class >> with: first with: second with: third with: fourth with: fifth
  ^ (self new: 5) at: 1 put: first; at: 2 put: second; at: 3 put: third; at: 4 put: fourth; at: 5 put: fifth; yourself.

Array class >> with: first with: second with: third with: fourth with: fifth with: sixth
  ^ (self new: 6) at: 1 put: first; at: 2 put: second; at: 3 put: third; at: 4 put: fourth; at: 5 put: fifth; at: 6 put: sixth; yourself.

"--- OrderedCollection ----------------------------------------------------------
 Insertion and removal at an index shift the tail through the primitive
 removeLast / addLast:."

OrderedCollection >> add: anObject beforeIndex: anIndex
  | tail |
  (anIndex between: 1 and: self size + 1)
    ifFalse: [^ self error: 'index ' , anIndex printString , ' out of bounds 1..' , (self size + 1) printString].
  tail := OrderedCollection new.
  [self size >= anIndex] whileTrue: [tail addFirst: self removeLast].
  self addLast: anObject.
  tail do: [:each | self addLast: each].
  ^ anObject.

OrderedCollection >> add: anObject afterIndex: anIndex
  ^ self add: anObject beforeIndex: anIndex + 1.

OrderedCollection >> removeAt: anIndex
  | tail removed |
  (anIndex between: 1 and: self size)
    ifFalse: [^ self error: 'index ' , anIndex printString , ' out of bounds 1..' , self size printString].
  tail := OrderedCollection new.
  [self size > anIndex] whileTrue: [tail addFirst: self removeLast].
  removed := self removeLast.
  tail do: [:each | self addLast: each].
  ^ removed.

OrderedCollection >> removeFirst: n
  "Remove the first n elements and answer them, in order, in an Array."
  | answer |
  n > self size ifTrue: [^ self error: 'removeFirst: ' , n printString , ' but the collection has only ' , self size printString , ' elements'].
  answer := Array new: n.
  1 to: n do: [:i | answer at: i put: self removeFirst].
  ^ answer.

OrderedCollection >> removeLast: n
  "Remove the last n elements and answer them, in order, in an Array."
  | answer |
  n > self size ifTrue: [^ self error: 'removeLast: ' , n printString , ' but the collection has only ' , self size printString , ' elements'].
  answer := Array new: n.
  n to: 1 by: -1 do: [:i | answer at: i put: self removeLast].
  ^ answer.

OrderedCollection >> addAllFirst: aCollection
  aCollection reverseDo: [:each | self addFirst: each].
  ^ aCollection.

OrderedCollection >> addAllLast: aCollection
  aCollection do: [:each | self addLast: each].
  ^ aCollection.

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

SequenceableCollection >> allButFirst: n
  ^ self copyFrom: n + 1 to: self size.

SequenceableCollection >> allButLast: n
  ^ self copyFrom: 1 to: self size - n.

SequenceableCollection >> first: n
  ^ self copyFrom: 1 to: n.

SequenceableCollection >> last: n
  ^ self copyFrom: self size - n + 1 to: self size.

SequenceableCollection >> indexOf: anObject
  1 to: self size do: [:i | (self at: i) = anObject ifTrue: [^ i]].
  ^ 0.

SequenceableCollection >> indexOf: anObject ifAbsent: aBlock
  | index |
  index := self indexOf: anObject.
  ^ index = 0 ifTrue: [aBlock value] ifFalse: [index].

SequenceableCollection >> copyReplacing: oldObject with: newObject
  ^ self collect: [:each | each = oldObject ifTrue: [newObject] ifFalse: [each]].

SequenceableCollection >> readStream
  ^ ReadStream on: self.

SequenceableCollection >> writeStream
  ^ WriteStream on: self.

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

Dictionary >> at: aKey ifPresent: aBlock
  (self includesKey: aKey) ifFalse: [^ nil].
  ^ aBlock cull: (self at: aKey).

Dictionary >> at: aKey ifPresent: presentBlock ifAbsent: absentBlock
  (self includesKey: aKey) ifFalse: [^ absentBlock value].
  ^ presentBlock cull: (self at: aKey).

Dictionary >> addAll: aCollection
  "Add a Dictionary's entries, or each Association of any other collection."
  (aCollection isKindOf: Dictionary)
    ifTrue: [aCollection keysAndValuesDo: [:k :v | self at: k put: v]]
    ifFalse: [aCollection do: [:each | self add: each]].
  ^ aCollection.

Dictionary >> removeAll
  self keys do: [:k | self removeKey: k].

"collect:, select: and reject: keep the keys and answer a Dictionary (Pharo)."

Dictionary >> collect: aBlock
  | out |
  out := self species new.
  self keysAndValuesDo: [:k :v | out at: k put: (aBlock value: v)].
  ^ out.

Dictionary >> select: aBlock
  | out |
  out := self species new.
  self keysAndValuesDo: [:k :v | (aBlock value: v) ifTrue: [out at: k put: v]].
  ^ out.

Dictionary >> reject: aBlock
  ^ self select: [:v | (aBlock value: v) not].

Dictionary class >> newFrom: aCollection
  "aCollection is a Dictionary or a collection of Associations."
  ^ self new addAll: aCollection; yourself.

"--- Association ---------------------------------------------------------------"

Association >> key: aKey value: aValue
  "protoST keeps a class's class-side and instance-side methods of the same
   selector in one slot, so this method serves both of Pharo's: sent to the
   class it creates an Association, sent to an instance it sets both fields."
  self isClass ifTrue: [^ self new key: aKey value: aValue].
  self key: aKey.
  self value: aValue.
  ^ self.

Association >> = anObject
  self == anObject ifTrue: [^ true].
  (anObject isKindOf: Association) ifFalse: [^ false].
  ^ self key = anObject key and: [self value = anObject value].

Association >> hash
  ^ (self key hash * 31 + self value hash) \\ 1073741824.

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

SortedCollection >> addLast: anObject
  ^ self shouldNotImplement.

SortedCollection >> add: anObject beforeIndex: anIndex
  ^ self shouldNotImplement.

"--- equality ----------------------------------------------------------------
 Sequences are equal when they are of the same species and hold equal
 elements in the same order (so an Interval equals the Array of its
 elements, as in Pharo); hashed collections when they hold equal elements.
 hash agrees with =."

Object >> ~= anObject
  ^ (self = anObject) not.

SequenceableCollection >> = other
  self == other ifTrue: [^ true].
  (other isKindOf: SequenceableCollection) ifFalse: [^ false].
  self species == other species ifFalse: [^ false].
  self size = other size ifFalse: [^ false].
  1 to: self size do: [:i | (self at: i) = (other at: i) ifFalse: [^ false]].
  ^ true.

SequenceableCollection >> hash
  | h |
  h := self size.
  1 to: (self size min: 8) do: [:i | h := (h * 31 + (self at: i) hash) \\ 1073741824].
  ^ h.

Set >> = other
  self == other ifTrue: [^ true].
  (other isKindOf: Set) ifFalse: [^ false].
  self size = other size ifFalse: [^ false].
  ^ self allSatisfy: [:each | other includes: each].

Set >> hash
  ^ self size.

Dictionary >> = other
  self == other ifTrue: [^ true].
  (other isKindOf: Dictionary) ifFalse: [^ false].
  self size = other size ifFalse: [^ false].
  self keysAndValuesDo: [:k :v |
    (other includesKey: k) ifFalse: [^ false].
    (other at: k) = v ifFalse: [^ false]].
  ^ true.

Dictionary >> hash
  ^ self size.

"--- Interval -----------------------------------------------------------------
 The native Interval is lazy, holds SmallInteger bounds, and offers no way to
 read back the bounds it was built from. It is therefore used only when its
 bounds can be recovered from its elements: SmallInteger bounds and step,
 at least two elements, and a stop that the stride reaches exactly (so start
 = first, step = second - first, stop = last). Every other interval (empty or
 single-element, an inexact stride, Float, Fraction or LargeInteger bounds)
 is a NumericInterval: an Interval subclass that keeps its bounds and computes
 its elements in protoST."

Interval class >> from: start to: stop
  ^ start to: stop.

Interval class >> from: start to: stop by: step
  ^ start to: stop by: step.

Interval >> species
  ^ Array.

Interval >> intervalStart
  ^ self first.

Interval >> intervalStop
  ^ self last.

Interval >> increment
  self size < 2 ifTrue: [^ 1].
  ^ (self at: 2) - self first.

Interval subclass: #NumericInterval instanceVariableNames: 'start stop step'.

NumericInterval class >> from: aStart to: aStop by: aStep
  aStep = 0 ifTrue: [^ self error: 'the step of an Interval must not be zero'].
  ^ self basicNew setFrom: aStart to: aStop by: aStep.

NumericInterval >> setFrom: aStart to: aStop by: aStep
  start := aStart.
  stop := aStop.
  step := aStep.
  ^ self.

NumericInterval >> intervalStart
  ^ start.

NumericInterval >> intervalStop
  ^ stop.

NumericInterval >> increment
  ^ step.

NumericInterval >> size
  ^ (((stop - start) / step) floor + 1) max: 0.

NumericInterval >> isEmpty
  ^ self size = 0.

NumericInterval >> notEmpty
  ^ self size > 0.

NumericInterval >> at: anIndex
  "Each element is computed from start, as Pharo does, so that floating-point
   error does not accumulate along the interval."
  (anIndex between: 1 and: self size)
    ifFalse: [^ self error: 'Interval>>at:: index ' , anIndex printString , ' out of range 1..' , self size printString].
  ^ start + ((anIndex - 1) * step).

NumericInterval >> first
  ^ self at: 1.

NumericInterval >> last
  ^ self at: self size.

NumericInterval >> do: aBlock
  1 to: self size do: [:i | aBlock value: start + ((i - 1) * step)].

NumericInterval >> asArray
  | array |
  array := Array new: self size.
  1 to: array size do: [:i | array at: i put: start + ((i - 1) * step)].
  ^ array.

"The native Collection primitives cannot iterate a NumericInterval, so the
 derived protocol runs over its elements as an Array."

NumericInterval >> collect: aBlock
  ^ self asArray collect: aBlock.

NumericInterval >> select: aBlock
  ^ self asArray select: aBlock.

NumericInterval >> reject: aBlock
  ^ self asArray reject: aBlock.

NumericInterval >> detect: aBlock
  ^ self asArray detect: aBlock.

NumericInterval >> detect: aBlock ifNone: noneBlock
  ^ self asArray detect: aBlock ifNone: noneBlock.

NumericInterval >> inject: initial into: aBlock
  ^ self asArray inject: initial into: aBlock.

NumericInterval >> count: aBlock
  ^ self asArray count: aBlock.

NumericInterval >> anySatisfy: aBlock
  ^ self asArray anySatisfy: aBlock.

NumericInterval >> allSatisfy: aBlock
  ^ self asArray allSatisfy: aBlock.

NumericInterval >> do: aBlock separatedBy: separatorBlock
  ^ self asArray do: aBlock separatedBy: separatorBlock.

NumericInterval >> , aCollection
  ^ self asArray , aCollection.

"Number loops and intervals: SmallInteger arguments keep the native
 primitives; anything else takes the general path. The general loop adds
 the step to a running value, as Pharo's to:by:do: does."

Integer >> isNativeIntervalTo: stop by: step
  "Private: whether the native Interval can represent self to: stop by: step
   faithfully (see the Interval section above)."
  "Early returns rather than and: keep this cheap: it runs on every to:."
  | span |
  self class == SmallInteger ifFalse: [^ false].
  stop class == SmallInteger ifFalse: [^ false].
  step class == SmallInteger ifFalse: [^ false].
  step = 0 ifTrue: [^ false].
  span := stop - self.
  span \\ step = 0 ifFalse: [^ false].
  ^ span // step >= 1.

Integer >> isSmallIntegerLoopTo: stop by: step
  "Private: whether the native to:by:do: primitive can run this loop."
  self class == SmallInteger ifFalse: [^ false].
  stop class == SmallInteger ifFalse: [^ false].
  ^ step class == SmallInteger.

Number >> numericTo: stop by: step do: aBlock
  "Private: the general to:by:do: loop for any kind of Number."
  | value |
  step = 0 ifTrue: [^ self error: 'the step of a loop must not be zero'].
  value := self.
  step > 0
    ifTrue: [[value <= stop] whileTrue: [aBlock value: value. value := value + step]]
    ifFalse: [[value >= stop] whileTrue: [aBlock value: value. value := value + step]].

Integer >> to: stop
  "The common case, inlined: with a step of 1 the stride is always exact."
  self class == SmallInteger ifTrue: [
    stop class == SmallInteger ifTrue: [
      stop > self ifTrue: [^ super to: stop]]].
  ^ NumericInterval from: self to: stop by: 1.

Integer >> to: stop by: step
  (self isNativeIntervalTo: stop by: step) ifTrue: [^ super to: stop by: step].
  ^ NumericInterval from: self to: stop by: step.

Integer >> to: stop do: aBlock
  (self isSmallIntegerLoopTo: stop by: 1) ifTrue: [^ super to: stop do: aBlock].
  self numericTo: stop by: 1 do: aBlock.

Integer >> to: stop by: step do: aBlock
  (self isSmallIntegerLoopTo: stop by: step) ifTrue: [^ super to: stop by: step do: aBlock].
  self numericTo: stop by: step do: aBlock.

Float >> to: stop
  ^ NumericInterval from: self to: stop by: 1.

Float >> to: stop by: step
  ^ NumericInterval from: self to: stop by: step.

Float >> to: stop do: aBlock
  self numericTo: stop by: 1 do: aBlock.

Float >> to: stop by: step do: aBlock
  self numericTo: stop by: step do: aBlock.

Fraction >> to: stop
  ^ NumericInterval from: self to: stop by: 1.

Fraction >> to: stop by: step
  ^ NumericInterval from: self to: stop by: step.

Fraction >> to: stop do: aBlock
  self numericTo: stop by: 1 do: aBlock.

Fraction >> to: stop by: step do: aBlock
  self numericTo: stop by: step do: aBlock.
