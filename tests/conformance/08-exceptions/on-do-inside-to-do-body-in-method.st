"EXPECT: 7"
"§8 + D29: inside a method, a handler in a to:do: body answers 3 on each of"
"the two iterations."
Object subclass: #Guarded.
Guarded >> run
  | s |
  s := 0.
  1 to: 2 do: [ :i | s := s + ([ Error signal: 'boom' ] on: Error do: [ :e | 3 ]) ].
  ^ s + 1.
Guarded new run.
