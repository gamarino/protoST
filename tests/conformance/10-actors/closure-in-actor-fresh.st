"EXPECT: 0 1 2"
"Fresh bindings hold for blocks created inside an actor's method."
Object subclass: #Maker.
Maker >> blocks
  | out |
  out := OrderedCollection new.
  0 to: 2 do: [:i | out add: [i]].
  ^ out.

bs := (Maker new asActor blocks) wait.
((bs at: 1) value) printString , ' ' , ((bs at: 2) value) printString , ' ' , ((bs at: 3) value) printString.
