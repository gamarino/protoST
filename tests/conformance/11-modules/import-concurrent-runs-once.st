"EXPECT: 1 same 56"
"S6 regression (§11.1, §11.2): eight actors import the same not-yet-loaded"
"module at the same moment. The module's top level runs exactly once (it"
"counts its runs in an Atom and sleeps 150 ms so the importers overlap);"
"every importer receives the same module object and can use its classes."
Object subclass: #LoadProbe
  instanceVariableNames: ''
  classVariableNames: 'runs'.
LoadProbe class >> reset  runs := Atom on: 0.
LoadProbe class >> bump  runs swap: [ :v | v + 1 ].
LoadProbe class >> runs  ^ runs value.
LoadProbe reset.
Object subclass: #Loader.
Loader >> load  ^ Import from: '_slow_once_mod'.
futs := OrderedCollection new.
1 to: 8 do: [ :i | futs add: Loader new asActor load ].
mods := OrderedCollection new.
futs do: [ :f | mods add: f wait ].
first := mods first.
same := true.
total := 0.
mods do: [ :m |
  (m == first) ifFalse: [ same := false ].
  total := total + m Once answer ].
verdict := same ifTrue: [ 'same' ] ifFalse: [ 'distinct' ].
(LoadProbe runs printString, ' ', verdict, ' ', total printString) printNl.
