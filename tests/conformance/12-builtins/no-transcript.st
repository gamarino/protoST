"XFAIL: EXPECT: ok"
"§12.9 + D10: Transcript is not provided. Spec-correct (Smalltalk-80): Transcript
should exist as an output stream; sending it show: should succeed and yield it.
Currently Transcript is an undefined global, so this is a runtime error."
Transcript show: 'hi'.
'ok'.
