"EXPECT: caught: doesNotUnderstand: ifTrue: (receiver class: SmallInteger)"
"§ Regression test for the 2026-05-24 perf-sprint inline of
ifTrue:/ifFalse:/ifTrue:ifFalse:/whileTrue:.  The compiler short-
circuits these sends to JUMP_IF_FALSE / JUMP_IF_TRUE bytecode for
literal-block arguments, eliminating the SEND dispatch.  Without a
type guard, a non-Boolean receiver would silently fall through to
the body branch — breaking the Smalltalk-standard semantic that
`123 ifTrue: [x]` raises doesNotUnderstand:ifTrue: because Integer
does not implement that selector.

The ASSERT_BOOL_OR_DNU opcode the compiler now emits before the
jump catches this and signals a normal MessageNotUnderstood,
matching the pre-inline SEND behaviour exactly.

This test fires the inline path (literal block argument), uses a
non-Boolean receiver, catches the signal, and asserts the message
text is the expected doesNotUnderstand:ifTrue: form."

result := [ 123 ifTrue: [ 'oops' ] ]
              on: Error
              do: [ :e | 'caught: ' , e messageText ].
^ result.
