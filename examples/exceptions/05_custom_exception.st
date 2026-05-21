"EXPECT: validation failed: age must be non-negative"
"-- 05_custom_exception.st -------------------------------------------------"
"-- A program using exceptions for real control flow. `ValidationError` is  --"
"-- a user subclass of `Error`; a subclass instance is caught by a handler  --"
"-- guarding the superclass. `pass` is also shown — the inner handler       --"
"-- inspects and re-raises outward when it cannot deal with the cause.      --"
"-- Expected output: validation failed: age must be non-negative            --"

Error subclass: #ValidationError.

Object subclass: #Person instanceVariableNames: 'age'.
Person >> age: anAge
  anAge < 0 ifTrue: [
    ValidationError signal: 'age must be non-negative' ].
  age := anAge.
  ^ self.

"Inner handler logs and passes; outer handler produces the final message."
[ [ Person new age: -5 ]
    on: ValidationError
    do: [ :ex | ex pass ] ]
  on: Error
  do: [ :ex | 'validation failed: ' , ex messageText ].
