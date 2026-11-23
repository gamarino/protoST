"EXPECT: #(true #('=') true 'refused' true)"
"Spec §6.3 (Task 4 review, fix round 1): the = that judges a losing
 write-once store is sent as a real send would be, so an installed value
 that does not understand = goes through its doesNotUnderstand:, whose answer
 is the comparison's: true makes the write silent, false signals M12. The
 class's = is removed with the private __installMethod:as: (a nil method
 makes the selector not understood); Object defines =, so this stands in for
 a foreign or proxy value."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> key
  ^ key
Object subclass: #NoEq instanceVariableNames: 'verdict seen'.
NoEq >> verdict: aBoolean
  verdict := aBoolean.
  seen := OrderedCollection new
NoEq >> seen
  ^ seen
NoEq >> doesNotUnderstand: aMessage
  seen add: aMessage selector.
  ^ verdict
NoEq __installMethod: nil as: #=.

| o first refusing o2 refused |
first := NoEq new verdict: true.
o := Holder new.
o setKey: first.
refusing := NoEq new verdict: false.
o2 := Holder new.
o2 setKey: refusing.
refused := [o2 setKey: 3. 'not refused'] on: ModificationForbidden do: [:e | 'refused'].
{ (o setKey: 3) == first. first seen asArray. o key == first. refused. o2 key == refusing }
