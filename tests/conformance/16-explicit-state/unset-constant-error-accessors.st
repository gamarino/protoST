"EXPECT: #(true true true 'constant ''owner'' of an Account read before it was set' 0 nil nil)"
"Spec §6.9, conformance 96: the signalled UnsetConstantError answers the
 object whose constant was read (receiver) and the constant's name as a
 String (variableName), and its messageText is M14's exact text. The text
 names the receiver as printString's default does, without sending the
 receiver's own printOn:, which here reads the unset constant again and would
 recurse. An instance a program makes itself answers nil for both."
Object subclass: #Account instanceVariableNames: 'printed' constantNames: 'number owner'.
Account >> owner
  ^ owner
Account >> printOn: aStream
  printed := (printed ifNil: [0]) + 1.
  aStream nextPutAll: 'Account of '; print: owner
Account >> printed
  ^ printed ifNil: [0]

| acct r |
acct := Account basicNew.
r := [acct owner] on: UnsetConstantError do: [:e |
  {e receiver == acct. e variableName = 'owner'. e variableName isString. e messageText}].
r , {acct printed. UnsetConstantError new receiver. UnsetConstantError new variableName}
