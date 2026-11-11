"EXPECT: true"
"An inlined conditional (ifTrue:, ifFalse:, ifTrue:ifFalse:, ifFalse:ifTrue:,
 and:, or: with literal blocks) behaves for a non-Boolean receiver as the
 send it replaces (the same selector with a block held in a variable): the
 same doesNotUnderstand: text, no block evaluated, and when the handler
 resumes the MessageNotUnderstood the resumed value is the value of the
 whole expression. Until 0.10.0 an inlined ifTrue: took the resumed value
 as its condition and ran its block. Each row is { inlined. sent } and the
 program answers whether every pair is equal; on a mismatch it prints them."
| log blk text resumed rows ok |
log := OrderedCollection new.
blk := [ log add: #ran. #block ].
text := [ :b | b on: MessageNotUnderstood do: [ :e | e messageText ] ].
resumed := [ :b | b on: MessageNotUnderstood do: [ :e | e resume: 42 ] ].
rows := {
  { text value: [ 3 and: [ log add: #ran. #block ] ]. text value: [ 3 and: blk ] }.
  { text value: [ nil or: [ log add: #ran. #block ] ]. text value: [ nil or: blk ] }.
  { text value: [ 'x' ifTrue: [ log add: #ran. #block ] ]. text value: [ 'x' ifTrue: blk ] }.
  { resumed value: [ 3 and: [ log add: #ran. #block ] ]. resumed value: [ 3 and: blk ] }.
  { resumed value: [ 3 or: [ log add: #ran. #block ] ]. resumed value: [ 3 or: blk ] }.
  { resumed value: [ 3 ifTrue: [ log add: #ran. #block ] ]. resumed value: [ 3 ifTrue: blk ] }.
  { resumed value: [ 3 ifFalse: [ log add: #ran. #block ] ]. resumed value: [ 3 ifFalse: blk ] }.
  { resumed value: [ 3 ifTrue: [ log add: #ran. #block ] ifFalse: [ log add: #ran. #other ] ].
    resumed value: [ 3 ifTrue: blk ifFalse: blk ] }.
  { resumed value: [ 3 ifFalse: [ log add: #ran. #block ] ifTrue: [ log add: #ran. #other ] ].
    resumed value: [ 3 ifFalse: blk ifTrue: blk ] }.
  { resumed value: [ (3 and: [ log add: #ran. #block ]) + 1 ]. resumed value: [ (3 and: blk) + 1 ] }.
  { log size. 0 } }.
ok := rows allSatisfy: [ :r | r first = r last ].
ok ifFalse: [ rows printNl ].
ok
