"EXPECT: true"
"One rule for every loop form whose condition answers an object that is not a
 Boolean (0.10.0): whileTrue: and whileFalse: compiled inline (literal
 blocks) or sent (a block held in a variable), and the unary whileTrue and
 whileFalse. Each signals NonBooleanReceiver, a resumable Error whose object
 is the condition's value; the loop takes the resumed value as true unless it
 is false, as Pharo does. Before, the inlined forms signalled
 doesNotUnderstand: of the loop selector to the value, the sent whileTrue:
 stopped silently and the sent whileFalse: ran its body. In every row the
 condition answers 3 on the first test, then n < 3 (or n >= 3); the handler
 resumes with the first entry. Each row is { inlined or sent. expected } and
 the program answers whether every pair is equal."
| n body cond fcond bodyBlk caught run rows ok |
caught := [ :blk | blk on: NonBooleanReceiver do: [ :e | { e class name. e object. e isResumable } ] ].
run := [ :resumeWith :blk | n := 0. body := 0.
  blk on: NonBooleanReceiver do: [ :e | e resume: resumeWith ].
  body ].
cond := [ n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n < 3 ] ].
fcond := [ n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n >= 3 ] ].
bodyBlk := [ body := body + 1 ].
rows := {
  "inlined whileTrue: / whileFalse:"
  { run value: true value: [ [ n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n < 3 ] ] whileTrue: [ body := body + 1 ] ]. 2 }.
  { run value: #yes value: [ [ n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n < 3 ] ] whileTrue: [ body := body + 1 ] ]. 2 }.
  { run value: false value: [ [ n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n < 3 ] ] whileTrue: [ body := body + 1 ] ]. 0 }.
  { run value: false value: [ [ n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n >= 3 ] ] whileFalse: [ body := body + 1 ] ]. 2 }.
  { run value: true value: [ [ n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n >= 3 ] ] whileFalse: [ body := body + 1 ] ]. 0 }.
  "sent whileTrue: / whileFalse:"
  { run value: true value: [ cond whileTrue: bodyBlk ]. 2 }.
  { run value: #yes value: [ cond whileTrue: bodyBlk ]. 2 }.
  { run value: false value: [ cond whileTrue: bodyBlk ]. 0 }.
  { run value: false value: [ fcond whileFalse: bodyBlk ]. 2 }.
  { run value: true value: [ fcond whileFalse: bodyBlk ]. 0 }.
  "unary whileTrue / whileFalse (the condition is the body)"
  { run value: true value: [ [ body := body + 1. n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n < 3 ] ] whileTrue ]. 3 }.
  { run value: false value: [ [ body := body + 1. n := n + 1. n = 1 ifTrue: [ 3 ] ifFalse: [ n >= 3 ] ] whileFalse ]. 3 }.
  "unhandled: the class, the object, resumable"
  { caught value: [ [ 3 ] whileTrue: [ nil ] ]. #('NonBooleanReceiver' 3 true) }.
  { caught value: [ [ 3 ] whileFalse: [ nil ] ]. #('NonBooleanReceiver' 3 true) }.
  { caught value: [ cond := [ 3 ]. cond whileTrue: bodyBlk ]. #('NonBooleanReceiver' 3 true) }.
  { caught value: [ [ 3 ] whileFalse ]. #('NonBooleanReceiver' 3 true) } }.
ok := rows allSatisfy: [ :r | r first = r last ].
ok ifFalse: [ rows printNl ].
ok
