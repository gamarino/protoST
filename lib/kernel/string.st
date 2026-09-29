"Kernel: String and Symbol protocol. A String is a sequenceable collection of
 Characters: at:, size, copyFrom:to:, case mapping, searching, splitting and
 parsing are primitive; iteration and the rest are defined here."

String >> asString
  ^ self.

String >> displayString
  self isClassObject ifTrue: [^ self name].
  ^ self.

String >> isSymbol
  "protoCore dispatches Symbol values to String; class tells them apart."
  ^ self class == Symbol.

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
  "Answer the lines of the receiver as an Array. CR, LF and CRLF each end a
   line; empty lines are kept, and a final line terminator adds no empty line."
  | out start i c cr lf |
  cr := Character cr.
  lf := Character lf.
  out := OrderedCollection new.
  start := 1.
  i := 1.
  [i <= self size] whileTrue: [
    c := self at: i.
    (c = cr or: [c = lf]) ifTrue: [
      out add: (self copyFrom: start to: i - 1).
      (c = cr and: [i < self size and: [(self at: i + 1) = lf]]) ifTrue: [i := i + 1].
      start := i + 1].
    i := i + 1].
  start <= self size ifTrue: [out add: (self copyFrom: start to: self size)].
  ^ out asArray.

String >> linesDo: aBlock
  self lines do: aBlock.

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

String class >> new
  "An empty String (a String is immutable, so every empty String will do)."
  ^ ''.

String class >> withAll: aCollection
  "A String of the Characters of aCollection."
  ^ String streamContents: [:s | aCollection do: [:c | s nextPut: c]].

String class >> cr
  ^ String with: Character cr.

String class >> lf
  ^ String with: Character lf.

String class >> crlf
  ^ String with: Character cr with: Character lf.

String class >> tab
  ^ String with: Character tab.

String class >> space
  ^ String with: Character space.

"--- immutability ---------------------------------------------------------------"

String >> at: anIndex put: aCharacter
  ^ ModificationForbidden new signal: 'Strings are immutable in protoST: at:put: cannot change a String. Build a new String with a WriteStream (String new writeStream, or String streamContents: [:stream | ...]), or copy it with copyReplaceAll:with: or copyReplacing:with:'.

"--- trimming -------------------------------------------------------------------"

String >> trimLeft
  | i |
  i := 1.
  [i <= self size and: [(self at: i) isSeparator]] whileTrue: [i := i + 1].
  ^ self copyFrom: i to: self size.

String >> trimRight
  | i |
  i := self size.
  [i >= 1 and: [(self at: i) isSeparator]] whileTrue: [i := i - 1].
  ^ self copyFrom: 1 to: i.

String >> trimSeparators
  ^ self trimLeft trimRight.

String >> trimmed
  ^ self trimSeparators.

"--- splitting ------------------------------------------------------------------"

String >> subStrings
  ^ self substrings.

String >> subStrings: separators
  "separators: a String of separator Characters, a Character, or a collection
   of Characters. Empty tokens are dropped."
  separators isString ifTrue: [^ self substrings: separators].
  separators isCharacter ifTrue: [^ self substrings: separators asString].
  ^ self substrings: (String withAll: separators).

String >> findTokens: delimiters
  ^ (self subStrings: delimiters) asOrderedCollection.

String >> splitOn: aSeparator
  "Pharo 8+: split at every occurrence of aSeparator and answer the pieces in
   an OrderedCollection, empty pieces included. aSeparator is a String (matched
   as a whole), a Character, or a one-argument Block that selects separator
   Characters."
  | pieces start index |
  pieces := OrderedCollection new.
  start := 1.
  aSeparator isBlock ifTrue: [
    1 to: self size do: [:i |
      (aSeparator value: (self at: i)) ifTrue: [
        pieces add: (self copyFrom: start to: i - 1).
        start := i + 1]].
    pieces add: (self copyFrom: start to: self size).
    ^ pieces].
  aSeparator isCharacter ifTrue: [^ self splitOn: [:c | c = aSeparator]].
  aSeparator isEmpty ifTrue: [^ pieces add: self; yourself].
  [(index := self findString: aSeparator startingAt: start) > 0] whileTrue: [
    pieces add: (self copyFrom: start to: index - 1).
    start := index + aSeparator size].
  pieces add: (self copyFrom: start to: self size).
  ^ pieces.

"--- formatting and case ----------------------------------------------------------"

String >> format: aCollection
  "Replace each {n} with (aCollection at: n) asString, or each {key} with
   (aCollection at: 'key') asString when aCollection is a Dictionary. A
   backslash makes the next character literal."
  | in out c key |
  in := ReadStream on: self.
  out := WriteStream on: String new.
  [in atEnd] whileFalse: [
    c := in next.
    c = ${
      ifTrue: [
        key := in upTo: $}.
        (key notEmpty and: [key allSatisfy: [:d | d isDigit]]) ifTrue: [key := key asInteger].
        out nextPutAll: (aCollection at: key) asString]
      ifFalse: [
        c = $\
          ifTrue: [in atEnd ifFalse: [out nextPut: in next]]
          ifFalse: [out nextPut: c]]].
  ^ out contents.

String >> capitalized
  self isEmpty ifTrue: [^ self].
  ^ self first asUppercase asString , (self copyFrom: 2 to: self size).

"--- searching (continued) ---------------------------------------------------------"

String >> includesSubString: aString
  ^ self includesSubstring: aString.

String >> findString: aString
  ^ self findString: aString startingAt: 1.

String >> findString: aString startingAt: start
  "Answer the index where aString next occurs at or after start, or 0."
  | n |
  n := aString size.
  n = 0 ifTrue: [^ 0].
  (self includesSubstring: aString) ifFalse: [^ 0].
  start to: self size - n + 1 do: [:i |
    (self copyFrom: i to: i + n - 1) = aString ifTrue: [^ i]].
  ^ 0.

String >> indexOf: aCharacter ifAbsent: aBlock
  | index |
  index := self indexOf: aCharacter.
  ^ index = 0 ifTrue: [aBlock value] ifFalse: [index].

String >> noneSatisfy: aBlock
  ^ (self anySatisfy: aBlock) not.

"--- copying and converting ---------------------------------------------------------"

String >> copyReplacing: oldCharacter with: newCharacter
  ^ self collect: [:c | c = oldCharacter ifTrue: [newCharacter] ifFalse: [c]].

String >> copyWithout: aCharacter
  ^ self reject: [:c | c = aCharacter].

String >> readStream
  ^ ReadStream on: self.

String >> writeStream
  ^ WriteStream on: self.

String >> asOrderedCollection
  | out |
  out := OrderedCollection new.
  self do: [:c | out add: c].
  ^ out.

String >> asSet
  | out |
  out := Set new.
  self do: [:c | out add: c].
  ^ out.

String >> asBag
  | out |
  out := Bag new.
  self do: [:c | out add: c].
  ^ out.

String >> asSortedCollection
  ^ self asArray asSortedCollection.

String >> asSortedCollection: aBlock
  ^ self asArray asSortedCollection: aBlock.

