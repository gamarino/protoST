"EXPECT: Array 0 true 0 #() #() #(1) true true #() | Array 0 #() | #[] 0 #() | '' 0 | (1 to: 0) 0 #() | #() 0 an OrderedCollection() 0 a Bag() 0 a SortedCollection() 0 | a SortedCollection() a SortedCollection(1 2 3) | #() 0 #(1)"
"S42: a built-in collection created without a size is a real, empty one, as
 in Pharo. Array new answered an instance with no element store; its size was
 0, but do:, collect:, select:, , and printString recursed until the stack
 depth was exceeded (the native iteration took it for a user collection and
 asked its do:, which is the native iteration). basicNew of Array,
 OrderedCollection, Bag and SortedCollection made the same storeless
 instance, so #() asSortedCollection failed and an empty SortedCollection
 recursed. Each group: the class, size, isEmpty, do: count, collect:,
 select:, , , = against #() both ways, printString; then a user subclass of
 Array, ByteArray (whose collect: answers an Array), String, Interval, the
 basicNew instances, and SortedCollection."
Array subclass: #MyArray instanceVariableNames: ''.
| a n out m |
a := Array new.
n := 0.
a do: [:each | n := n + 1].
out := WriteStream on: String new.
out
  nextPutAll: a class printString; space;
  nextPutAll: a size printString; space;
  nextPutAll: a isEmpty printString; space;
  nextPutAll: n printString; space;
  nextPutAll: (a collect: [:x | x]) printString; space;
  nextPutAll: (a select: [:x | true]) printString; space;
  nextPutAll: (a , #(1)) printString; space;
  nextPutAll: (a = #()) printString; space;
  nextPutAll: (#() = a) printString; space;
  nextPutAll: a printString; nextPutAll: ' | '.
m := MyArray new.
out
  nextPutAll: m class superclass printString; space;
  nextPutAll: m size printString; space;
  nextPutAll: (m collect: [:x | x]) printString; nextPutAll: ' | '.
out
  nextPutAll: ByteArray new printString; space;
  nextPutAll: ByteArray new size printString; space;
  nextPutAll: (ByteArray new collect: [:x | x]) printString; nextPutAll: ' | '.
out
  nextPutAll: String new printString; space;
  nextPutAll: String new size printString; nextPutAll: ' | '.
out
  nextPutAll: Interval new printString; space;
  nextPutAll: Interval new size printString; space;
  nextPutAll: (Interval new collect: [:x | x]) printString; nextPutAll: ' | '.
{Array basicNew. OrderedCollection basicNew. Bag basicNew. SortedCollection basicNew} do: [:c |
  out
    nextPutAll: c printString; space;
    nextPutAll: (c inject: 0 into: [:sum :x | sum + 1]) printString; space].
out nextPutAll: '| '.
out
  nextPutAll: #() asSortedCollection printString; space;
  nextPutAll: #(3 1 2) asSortedCollection printString; nextPutAll: ' | '.
a := Array basicNew.
out
  nextPutAll: (a collect: [:x | x]) printString; space;
  nextPutAll: (a select: [:x | true]) size printString; space;
  nextPutAll: (a , #(1)) printString.
out contents.
