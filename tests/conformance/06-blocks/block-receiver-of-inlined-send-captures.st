"EXPECT: #(85 85 #(42 42))"
"A block literal that is the receiver of and:, or:, ifTrue: or ifNil: is not
 inlined (only whileTrue: / whileFalse: inline their receiver block): it is a
 real closure, and the method's variables it uses are captured like those of
 any other block (review L3). Here the block reaches the receiver's
 doesNotUnderstand: and is evaluated by the handler after the method's frame
 has moved on."
Object subclass: #T.
T >> m | x | x := 42. ^ [ [ x ] and: [ x + 1 ] ] on: MessageNotUnderstood do: [ :e | e receiver value + e message arguments first value ].
T >> o | x | x := 42. ^ [ [ x ] or: [ x + 1 ] ] on: MessageNotUnderstood do: [ :e | e receiver value + e message arguments first value ].
T >> k | x r | x := 41. r := [ x := x + 1. x ] ifNil: [ 0 ]. ^ { r value. x }.
{ T new m. T new o. T new k }
