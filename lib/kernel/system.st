"Kernel: Transcript and Smalltalk.

 Transcript writes to standard output, in program order with printNl.
 Smalltalk gives access to the global namespace."

Object subclass: #TranscriptStream.

TranscriptStream >> show: anObject
  self __stdout: anObject displayString.
  ^ self.

TranscriptStream >> nextPutAll: aString
  ^ self show: aString.

TranscriptStream >> nextPut: aCharacter
  ^ self show: aCharacter asString.

TranscriptStream >> print: anObject
  ^ self show: anObject printString.

TranscriptStream >> display: anObject
  ^ self show: anObject displayString.

TranscriptStream >> << anObject
  ^ self show: anObject.

TranscriptStream >> cr
  ^ self show: (String with: Character lf).

TranscriptStream >> nl
  ^ self cr.

TranscriptStream >> lf
  ^ self cr.

TranscriptStream >> space
  ^ self show: ' '.

TranscriptStream >> tab
  ^ self show: (String with: Character tab).

TranscriptStream >> showCr: anObject
  ^ self show: anObject; cr.

TranscriptStream >> flush
  ^ self.

TranscriptStream >> printOn: aStream
  aStream nextPutAll: 'Transcript'.

"A top-level assignment in a file binds a local of that file, so the kernel
 publishes its globals explicitly."
nil __globalAt: #Transcript put: TranscriptStream new.

Object subclass: #SmalltalkImage.

SmalltalkImage >> at: aSymbol
  (self __globalIncludes: aSymbol) ifFalse: [^ self error: 'undefined global: ' , aSymbol asString].
  ^ self __globalAt: aSymbol.

SmalltalkImage >> at: aSymbol ifAbsent: aBlock
  (self __globalIncludes: aSymbol) ifFalse: [^ aBlock value].
  ^ self __globalAt: aSymbol.

SmalltalkImage >> at: aSymbol put: anObject
  ^ self __globalAt: aSymbol put: anObject.

SmalltalkImage >> includesKey: aSymbol
  ^ self __globalIncludes: aSymbol.

SmalltalkImage >> printOn: aStream
  aStream nextPutAll: 'Smalltalk'.

nil __globalAt: #Smalltalk put: SmalltalkImage new.
