"EXPECT: ok 600 40"
"K15: collect: nested 600 deep while collections run. Every level's collect:
 keeps its result list rooted by a transient pin while its block runs the
 level below. Up to 0.9.0 only 256 pins could be live; the levels past that
 held their list in a C++ local only, a collection freed it, and appending to
 it crashed (SIGSEGV in ProtoList::appendLast, or 'stack smashing detected').
 Each level allocates some garbage so that the young generation is handed to
 the collector during the walk: 0.9.0 crashed on 10 runs out of 10."
Object subclass: #Walker.
Walker >> wrap: n around: seed
  | v |
  v := seed.
  n timesRepeat: [v := Array with: v].
  ^ v.
Walker >> walk: v
  | junk |
  v isInteger ifTrue: [^ v + 1].
  junk := OrderedCollection new.
  1 to: 20 do: [:k | junk add: (Array new: 8)].
  ^ v collect: [:each | self walk: each].
Walker >> depthOf: v leaf: aBlock
  | x d |
  x := v. d := 0.
  [x isInteger] whileFalse: [x size = 1 ifFalse: [^ -1]. x := x first. d := d + 1].
  aBlock value: x.
  ^ d.
| w good leaf depth |
w := Walker new.
good := 0.
1 to: 40 do: [:i |
  depth := w depthOf: (w walk: (w wrap: 600 around: i)) leaf: [:x | leaf := x].
  (depth = 600 and: [leaf = (i + 1)]) ifTrue: [good := good + 1]].
'ok ', depth printString, ' ', good printString.
