"EXPECT: #(false 'mine' true 'mine' 'mine' 3 #(1 2))"
"`==` is identity and is never looked up (0.10.0), as in Pharo, where
 ProtoObject>>== says 'No Lookup. Do not override'. A class's own == is
 reached only by a real send (perform:). Likewise an ifNil: family send with
 literal blocks is compiled inline and tests identity with nil, as Pharo's
 compiler does, so a class's own ifNil: is reached only when the blocks are
 not literals. ifNotNil:ifNil: is bound for the sent form (it was not)."
Object subclass: #Odd.
Odd >> == other ^ #mine.
Odd >> ifNil: aBlock ^ #mine.
| o b |
o := Odd new.
b := [ #nilArm ].
{ o == 3.
  o perform: #== with: 3.
  o == o.
  o ifNil: b.
  o perform: #ifNil: with: b.
  (o ifNil: [ #nilArm ]) == o ifTrue: [ 3 ] ifFalse: [ 4 ].
  { 1 ifNotNil: [ :v | v ] ifNil: [ 0 ]. 2 ifNotNil: [ :v | v ] ifNil: b } }
