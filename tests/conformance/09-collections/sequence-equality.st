"EXPECT: true false true false true"
"Sequences compare element by element; ~= is the negation of the receiver's =."
Object subclass: #Q.
Q >> = other
  ^ true.

(#(1 2 3) = #(1 2 3)) printString , ' ' , (Q new ~= Q new) printString , ' ' , ((OrderedCollection new add: 1; yourself) = (OrderedCollection new add: 1; yourself)) printString , ' ' , (#(1 2) = #(1 2 3)) printString , ' ' , (#(1 $a 'x') = #(1 $a 'x')) printString.
