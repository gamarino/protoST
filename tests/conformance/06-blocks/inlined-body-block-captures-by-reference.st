"EXPECT: #(5 3 7)"
"A variable used only by inlined control bodies stays a plain temporary, and
 one that a real block inside an inlined body captures is still shared by
 reference: the block created in the ifTrue: body sees the later assignment,
 and it can assign the variable too."
Object subclass: #F.
F >> test
  | s b n w |
  s := 0. n := 0.
  true ifTrue: [ b := [ s ]. w := [ :v | s := v ] ].
  1 to: 3 do: [:k | n := n + 1 ].
  s := 5.
  ^ { b value. n. [ w value: 7. s ] value }
F new test
