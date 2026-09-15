"-- a module whose top level raises an Error while ImportProbe failing is"
"-- true, and exports Recovered otherwise — used by import-error-retry.st and"
"-- import-error-retries-load.st --"
ImportProbe noteRun.
ImportProbe failing ifTrue: [ Error signal: 'boom in module' ].
Object subclass: #Recovered.
Recovered class >> answer
  ^ 42.
