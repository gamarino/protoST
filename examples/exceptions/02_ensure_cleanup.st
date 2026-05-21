"EXPECT: opened-used-closed"
"-- 02_ensure_cleanup.st ---------------------------------------------------"
"-- `ensure:` registers a cleanup block that runs whether the protected     --"
"-- block completes normally or is abandoned by an unwind. Here the         --"
"-- protected block raises, the handler catches it, and the `ensure:`       --"
"-- cleanup still runs during the unwind — the resource is always closed.   --"
"-- Expected output: opened-used-closed                                     --"

Object subclass: #Resource instanceVariableNames: 'log'.
Resource >> initialize
  log := OrderedCollection new.
  ^ self.
Resource >> note: aString
  log add: aString.
  ^ self.
Resource >> trail
  ^ log inject: '' into: [ :acc :s |
      (acc size = 0) ifTrue: [ s ] ifFalse: [ acc , '-' , s ] ].

r := Resource new.
r initialize.

[ [ r note: 'opened'.
    Error signal: 'mid-use failure'.
    r note: 'never' ]
    ensure: [ r note: 'closed' ] ]
  on: Error
  do: [ :ex | r note: 'used' ].

r trail.
