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
