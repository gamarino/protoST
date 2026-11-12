"EXPECT: true"
"Control blocks that declare temporaries (ifTrue:, ifFalse:, ifTrue:ifFalse:,
 and:, or:, whileTrue:, whileFalse:, to:do:, the nil tests) are compiled
 inline since 0.10.0; the meaning is the send's (LANGUAGE.md 6.3): every
 evaluation of the block has its own temporaries, starting nil, also when
 the block runs again in a loop or inside another inlined block; a
 temporary that reuses the name of a method argument, temporary, captured
 variable or instance variable is a distinct variable; a block that keeps a
 temporary in a closure created in each iteration sees that iteration's
 binding; a non-Boolean receiver gets the real send. Each row is
 { inlined. sent }, the sent form passing blocks held in variables; the
 program answers whether every pair is equal, and prints them otherwise."
Object subclass: #F instanceVariableNames: 'iv'.
F >> argShadow: x
  true ifTrue: [ | x | x := 3 ].
  ^ x
F >> ivShadow
  iv := 7.
  true ifTrue: [ | iv | iv := 3 ].
  ^ { iv. self ivRead }
F >> ivRead
  ^ iv
F >> capturedShadow
  | x b |
  x := 1. b := [ x ].
  true ifTrue: [ | x | x := 2 ].
  ^ { b value. x }
F >> freshInLoop
  | n |
  n := 0.
  1 to: 3 do: [ :i | true ifTrue: [ | t | t ifNil: [ n := n + 1 ]. t := i ] ].
  ^ n
F >> freshInWhile
  | k nils |
  k := 0. nils := 0.
  [ | c | c ifNil: [ nils := nils + 1 ]. c := k. c < 3 ]
    whileTrue: [ | t | t ifNil: [ nils := nils + 10 ]. t := k. k := t + 1 ].
  ^ { k. nils }
F >> freshToDo
  | nils |
  nils := 0.
  1 to: 4 do: [ :i | | t | t ifNil: [ nils := nils + 1 ]. t := i ].
  ^ nils
F >> perIterationClosures
  | bs |
  bs := OrderedCollection new.
  1 to: 3 do: [ :i | | t | t := i * 10. bs add: [ t ] ].
  ^ (bs collect: [ :b | b value ]) asArray
F >> perIterationClosuresWhile
  | bs k |
  bs := OrderedCollection new. k := 0.
  [ k < 3 ] whileTrue: [ | t | k := k + 1. t := k. bs add: [ t ] ].
  ^ (bs collect: [ :b | b value ]) asArray
F >> nested: c
  ^ c ifTrue: [ | a | a := 1. c ifTrue: [ | b | b := a + 1. { a. b } ] ]
F >> nonLocal: c
  c ifTrue: [ | t | t := #early. ^ t ].
  ^ #late
F >> nilArms: x
  ^ x ifNil: [ | t | t := 0. t ] ifNotNil: [ :v | | t | t := v * 2. t ]
| f blk1 blk2 rows ok text |
f := F new.
text := [ :b | b on: MessageNotUnderstood do: [ :e | e messageText ] ].
blk1 := [ | t | t := 1. t ].
blk2 := [ | t | t := 2. t ].
rows := {
  { f argShadow: 5. 5 }.
  { f ivShadow. #(7 7) }.
  { f capturedShadow. #(1 1) }.
  { f freshInLoop. 3 }.
  { f freshInWhile. #(3 34) }.
  { f freshToDo. 4 }.
  { f perIterationClosures. #(10 20 30) }.
  { f perIterationClosuresWhile. #(1 2 3) }.
  { f nested: true. #(1 2) }.
  { f nested: false. nil }.
  { f nonLocal: true. #early }.
  { f nonLocal: false. #late }.
  { f nilArms: nil. 0 }.
  { f nilArms: 4. 8 }.
  { true ifTrue: [ | t | t := 1. t ] ifFalse: [ | t | t := 2. t ]. true ifTrue: blk1 ifFalse: blk2 }.
  { false ifTrue: [ | t | t := 1. t ] ifFalse: [ | t | t := 2. t ]. false ifTrue: blk1 ifFalse: blk2 }.
  { true and: [ | t | t := 1. t = 1 ]. true }.
  { false or: [ | t | t := 1. t = 2 ]. false }.
  { text value: [ 3 ifTrue: [ | t | t := 1. t ] ]. text value: [ 3 ifTrue: blk1 ] } }.
ok := rows allSatisfy: [ :r | r first = r last ].
ok ifFalse: [ rows printNl ].
ok
