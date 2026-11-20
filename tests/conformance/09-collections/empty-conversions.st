"EXPECT: a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() a SortedCollection() | a SortedCollection(5 3 1) a SortedCollection(5 3 1) a SortedCollection(5 3 1) a SortedCollection(1 3 5) | a Bag() a Bag() a Bag() a Bag() a Bag() | a Set() a Set() a Set() a Set() a Set() | an OrderedCollection() an OrderedCollection() an OrderedCollection() an OrderedCollection() an OrderedCollection() | #() #() #() #() #() #() | #() #()"
"S44: asSortedCollection: on an empty collection signalled 'sort: needs an
 Array or an OrderedCollection (use sorted)' (found by protoBaseST at 0.12.0,
 which used sorted: instead). The SortedCollection that sortBlock: builds
 with basicNew had no element store yet; addAll: of an empty receiver added
 nothing, so the sort: that follows it found no store and refused. The S42
 fix (an Array, OrderedCollection or Bag with no store is an empty
 list-backed one) removed the cause in 0.14.0; this test keeps it removed.
 Groups: asSortedCollection: and asSortedCollection on every empty
 collection kind; the sort block of an empty conversion is kept for later
 adds; the sibling conversions (asBag, asSet, asOrderedCollection, asArray)
 on empty receivers; sorted and sorted: on empty receivers."
| out descending emptySources |
out := WriteStream on: String new.
emptySources := {#(). Array new. Set new. OrderedCollection new. Bag new. Dictionary new. (1 to: 0). ''. SortedCollection new}.
emptySources do: [:c |
  out nextPutAll: (c asSortedCollection: [:a :b | a <= b]) printString; space].
{#(). Set new. OrderedCollection new. Bag new} do: [:c |
  out nextPutAll: c asSortedCollection printString; space].
out nextPutAll: '| '.
descending := [:a :b | a >= b].
{#(). Set new. OrderedCollection new} do: [:c |
  out nextPutAll: ((c asSortedCollection: descending) add: 1; add: 5; add: 3; yourself) printString; space].
out nextPutAll: (#() asSortedCollection add: 5; add: 1; add: 3; yourself) printString; nextPutAll: ' | '.
{#(). Set new. OrderedCollection new. Dictionary new. (1 to: 0)} do: [:c |
  out nextPutAll: c asBag printString; space].
out nextPutAll: '| '.
{#(). OrderedCollection new. Bag new. Dictionary new. (1 to: 0)} do: [:c |
  out nextPutAll: c asSet printString; space].
out nextPutAll: '| '.
{#(). Set new. Bag new. SortedCollection new. (1 to: 0)} do: [:c |
  out nextPutAll: c asOrderedCollection printString; space].
out nextPutAll: '| '.
{Set new. Bag new. SortedCollection new. OrderedCollection new. Dictionary new. (1 to: 0)} do: [:c |
  out nextPutAll: c asArray printString; space].
out nextPutAll: '| '.
out
  nextPutAll: #() sorted printString; space;
  nextPutAll: (Set new sorted: [:a :b | a <= b]) printString.
out contents.
