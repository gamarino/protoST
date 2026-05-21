"EXPECT: connected on attempt 3"
"-- 03_resume_and_retry.st -------------------------------------------------"
"-- Two handler actions for real control flow. `retry` re-runs the          --"
"-- protected block from the start — used here to retry a flaky connection  --"
"-- until it succeeds. (`resume:` continues past the signal point instead;  --"
"-- see 04_warning_resume.st.)                                              --"
"-- Expected output: connected on attempt 3                                 --"

Object subclass: #FlakyLink instanceVariableNames: 'attempts'.
FlakyLink >> initialize
  attempts := 0.
  ^ self.
FlakyLink >> connect
  "Fails the first two times, succeeds on the third."
  attempts := attempts + 1.
  attempts < 3 ifTrue: [ Error signal: 'timeout' ].
  ^ 'connected on attempt ' , attempts printString.

link := FlakyLink new.
link initialize.

[ link connect ]
  on: Error
  do: [ :ex | ex retry ].
