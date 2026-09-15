"EXPECT: cyclic cyclic"
"S6 (§11.2): two actors start the two halves of an import cycle at the same"
"moment — one imports _cycle_a_mod, the other _cycle_b_mod, and each module"
"imports the other after a 100 ms sleep. Each actor ends up waiting for a"
"module whose load the other holds (or, if one runs ahead, re-enters its own)."
"Both imports must fail with a catchable cyclic-import Error instead of"
"waiting forever. Which module the message names depends on the interleaving."
"The handler records the FIRST error it sees: an error raised inside a nested"
"module's top level currently reaches the importer's handler a second time"
"with a different message (open bug D28 in docs/STATUS.md)."
Object subclass: #CycleImporter
  instanceVariableNames: 'first'.
CycleImporter >> import: aName
  [ Import from: aName ]
    on: Error
    do: [ :e | first isNil ifTrue: [ first := e messageText ] ].
  ^ ((first = 'cyclic module import: _cycle_a_mod')
      or: [ first = 'cyclic module import: _cycle_b_mod' ])
    ifTrue: [ 'cyclic' ]
    ifFalse: [ first printString ].
fa := CycleImporter new asActor import: '_cycle_a_mod'.
fb := CycleImporter new asActor import: '_cycle_b_mod'.
(fa wait, ' ', fb wait) printNl.
