"A thenDo: callback, run by a resolve: inside a Warning handler, sends
 resume: to that handler's exception. The callback is not the handler: the
 exception has no active handler for it, so the resume: is an error reported
 as a callback error, and the handler falls off its end."
| f r |
f := Future new.
r := [(Warning signal: 'w') printString] on: Warning do: [:e |
   f thenDo: [:v | e resume: 'from callback'].
   f resolve: 1.
   'handler fell off'].
r printString displayNl.
'done'
