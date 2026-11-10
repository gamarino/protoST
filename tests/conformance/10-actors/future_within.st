"EXPECT: timeout"
"§10 waitWithin: on a Future that never settles signals TimeoutError, an
 Error subclass, once the deadline passes."
[(Future new) waitWithin: 50. 'settled']
  on: TimeoutError
  do: [:e | (e isKindOf: Error) ifTrue: ['timeout'] ifFalse: ['not an Error']]
