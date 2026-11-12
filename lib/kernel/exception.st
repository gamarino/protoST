"Kernel: exception protocol. signal, signal:, on:do:, retry, retryUsing:,
 resume:, pass, outer, return: and ensure: are native; exception sets and the
 remaining accessors are defined here."

Object subclass: #ExceptionSet instanceVariableNames: 'exceptionClasses'.

ExceptionSet >> add: anExceptionClass
  exceptionClasses := (exceptionClasses ifNil: [#()]) copyWith: anExceptionClass.
  ^ anExceptionClass.

ExceptionSet >> , anExceptionClass
  self add: anExceptionClass.
  ^ self.

ExceptionSet >> printOn: aStream
  exceptionClasses do: [:each | aStream nextPutAll: each name] separatedBy: [aStream nextPutAll: ', '].

Exception class >> , anExceptionClass
  ^ ExceptionSet new add: self; add: anExceptionClass; yourself.

Exception >> return
  ^ self return: nil.

Exception >> description
  ^ self messageText.

Exception >> signalerContext
  ^ self error: 'thisContext is not available in protoST'.

Message >> printOn: aStream
  aStream nextPutAll: 'a Message('; print: self selector; nextPut: $).

"Signalled on an attempt to modify an immutable object, such as a String."
Error subclass: #ModificationForbidden.

"Signalled by indexed access outside a collection's bounds."
Error subclass: #SubscriptOutOfBounds.

"Signalled by a Dictionary lookup or removal of a key it does not hold."
Error subclass: #KeyNotFound.

"Signalled by remove: of an element the collection does not hold."
Error subclass: #NotFound.

"Signalled when a deadline passes first: by waitWithin:, and as the rejection
 of the Future answered by within: (lib/kernel/future.st)."
Error subclass: #TimeoutError.

"Signalled by protoCore for a malformed argument (an unparsable integer
 literal, a radix outside 2..36, a negative shift) or an operand of the wrong
 type for an integer operation; the runtime's error factory builds it."
Error subclass: #InvalidArgument.

Exception >> isResumable
  ^ self __isResumable.

"Signalled by resume: (and so by a resumption through outer or pass) of an
 exception that is not resumable, inside the handler that tried it, as in
 Pharo. It is an Exception, not an Error, and is itself not resumable."
Exception subclass: #IllegalResumeAttempt.

IllegalResumeAttempt >> isResumable
  ^ false.

"A Notification is a resumable Exception whose unhandled default action
 answers nil and reports nothing, as in Pharo. The runtime's default action
 for an unhandled resumable exception prints the exception's stored text on
 stderr, so a Notification keeps its text in its own instance variable
 (answered by messageText and description) rather than in that slot."
Exception subclass: #Notification instanceVariableNames: 'notificationText'.

Notification class >> signal: aText
  ^ self new signal: aText.

Notification >> signal: aText
  notificationText := aText.
  ^ self signal.

Notification >> messageText: aText
  notificationText := aText.

Notification >> messageText
  ^ notificationText ifNil: [super messageText].

"Signalled when a loop condition (whileTrue:, whileFalse:, whileTrue,
 whileFalse; compiled inline or sent) answers an object that is not a
 Boolean, as in Pharo. Resumable: the loop takes the resumed value as true
 unless it is false. object answers the condition's value."
NonBooleanReceiver >> object
  ^ self instVarNamed: 'object'.
