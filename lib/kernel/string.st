"Kernel: String and Symbol protocol. A String is a sequenceable collection of
 Characters: at:, size, copyFrom:to:, case mapping, searching, splitting and
 parsing are primitive; iteration and the rest are defined here."

String >> asString
  ^ self.

String >> displayString
  ^ self.

String >> isSymbol
  ^ false.

String >> isEmpty
  ^ self size = 0.

String >> notEmpty
  ^ self size > 0.

String >> ifEmpty: aBlock
  ^ self isEmpty ifTrue: [aBlock value] ifFalse: [self].

String >> first
  ^ self at: 1.

String >> last
  ^ self at: self size.

String >> first: n
  ^ self copyFrom: 1 to: n.

String >> last: n
  ^ self copyFrom: self size - n + 1 to: self size.

String >> allButFirst
  ^ self copyFrom: 2 to: self size.

String >> allButLast
  ^ self copyFrom: 1 to: self size - 1.

String >> reverse
  ^ self reversed.

"--- iteration ----------------------------------------------------------------"

String >> do: aBlock
  1 to: self size do: [:i | aBlock value: (self at: i)].

String >> reverseDo: aBlock
  self size to: 1 by: -1 do: [:i | aBlock value: (self at: i)].

String >> withIndexDo: aBlock
  1 to: self size do: [:i | aBlock value: (self at: i) value: i].

String >> doWithIndex: aBlock
  ^ self withIndexDo: aBlock.

String >> keysAndValuesDo: aBlock
  1 to: self size do: [:i | aBlock value: i value: (self at: i)].

String >> do: aBlock separatedBy: separatorBlock
  1 to: self size do: [:i |
    i > 1 ifTrue: [separatorBlock value].
    aBlock value: (self at: i)].

String >> collect: aBlock
  | out |
  out := ''.
  self do: [:c | out := out , (aBlock value: c) asString].
  ^ out.

String >> select: aBlock
  | out |
  out := ''.
  self do: [:c | (aBlock value: c) ifTrue: [out := out , c asString]].
  ^ out.

String >> reject: aBlock
  ^ self select: [:c | (aBlock value: c) not].

String >> detect: aBlock ifNone: noneBlock
  self do: [:c | (aBlock value: c) ifTrue: [^ c]].
  ^ noneBlock value.

String >> detect: aBlock
  ^ self detect: aBlock ifNone: [self error: 'no element satisfies the condition'].

String >> inject: initial into: aBlock
  | acc |
  acc := initial.
  self do: [:c | acc := aBlock value: acc value: c].
  ^ acc.

String >> count: aBlock
  ^ self inject: 0 into: [:n :c | (aBlock value: c) ifTrue: [n + 1] ifFalse: [n]].

String >> anySatisfy: aBlock
  self do: [:c | (aBlock value: c) ifTrue: [^ true]].
  ^ false.

String >> allSatisfy: aBlock
  self do: [:c | (aBlock value: c) ifFalse: [^ false]].
  ^ true.

"--- searching ----------------------------------------------------------------"

String >> indexOf: aCharacter
  1 to: self size do: [:i | (self at: i) = aCharacter ifTrue: [^ i]].
  ^ 0.

String >> includes: aCharacter
  ^ (self indexOf: aCharacter) > 0.

String >> occurrencesOf: aCharacter
  ^ self count: [:c | c = aCharacter].

String >> beginsWith: prefix
  ^ self size >= prefix size and: [(self copyFrom: 1 to: prefix size) = prefix].

String >> endsWith: suffix
  ^ self size >= suffix size
      and: [(self copyFrom: self size - suffix size + 1 to: self size) = suffix].

"--- building -----------------------------------------------------------------"

String >> copyWith: aCharacter
  ^ self , aCharacter asString.

String >> asArray
  | out |
  out := OrderedCollection new.
  self do: [:c | out add: c].
  ^ out asArray.

String >> lines
  ^ self substrings: (String with: Character lf with: Character cr).

String >> join: aCollection
  | out first |
  out := ''.
  first := true.
  aCollection do: [:each |
    first ifFalse: [out := out , self].
    out := out , each asString.
    first := false].
  ^ out.

String class >> with: aCharacter
  ^ aCharacter asString.

String class >> with: first with: second
  ^ first asString , second asString.

String class >> new: size withAll: aCharacter
  | out |
  out := ''.
  size timesRepeat: [out := out , aCharacter asString].
  ^ out.

String class >> new: size
  ^ String new: size withAll: Character space.

"--- Symbol -------------------------------------------------------------------"

Symbol >> isSymbol
  ^ true.

Symbol >> asSymbol
  ^ self.
