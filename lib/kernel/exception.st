"Kernel: exception protocol. signal, signal:, on:do:, retry, resume:, pass,
 return: and ensure: are native; exception sets and the remaining accessors
 are defined here."

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

Exception >> isResumable
  ^ self __isResumable.

"Evaluate replacementBlock in place of the protected block, as retry
 re-evaluates the protected block."
Exception >> retryUsing: replacementBlock
  ^ self return: replacementBlock value.

"A Notification is a resumable Exception whose unhandled default action
 answers nil and reports nothing, as in Pharo. The runtime's default action
 for an unhandled resumable exception prints the exception's stored text on
 stderr, so a Notification keeps its text in its own instance variable
 (answered by messageText and description) rather than in that slot."
Exception subclass: #Notification instanceVariableNames: 'notificationText'.

"protoST keeps a class's class-side and instance-side methods of the same
 selector in one slot, so this method serves both Notification signal: 'text'
 and aNotification signal: 'text'."
Notification >> signal: aText
  self isClass ifTrue: [^ self new signal: aText].
  notificationText := aText.
  ^ self signal.

Notification >> messageText: aText
  notificationText := aText.

Notification >> messageText
  ^ notificationText ifNil: [super messageText].
