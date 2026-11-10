"EXPECT: timeout: the Future did not settle within 50 ms"
"§10 waitWithin: on a Future that never settles signals TimeoutError, an
 Error subclass, once the deadline passes; its messageText states the
 deadline."
[(Future new) waitWithin: 50. 'settled']
  on: TimeoutError
  do: [:e | (e isKindOf: Error)
    ifTrue: ['timeout: ' , e messageText]
    ifFalse: ['not an Error']]
