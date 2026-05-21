"EXPECT: 42"
"-- 04_warning_resume.st ---------------------------------------------------"
"-- `resume:` is the resumption handler action: it makes `signal` RETURN a  --"
"-- value and the protected computation CONTINUE from the signal point.     --"
"-- Only a resumable exception (Warning, Exception — not Error) may be      --"
"-- resumed. Here a Warning signals mid-expression and the handler supplies --"
"-- the value to continue with.                                             --"
"-- Expected output: 42                                                     --"

[ | base |
  base := Warning signal: 'use a default'.
  base * 2 ]
  on: Warning
  do: [ :ex | ex resume: 21 ].
