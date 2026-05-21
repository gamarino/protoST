"EXPECT: recovered: disk full"
"-- 01_on_do_basics.st -----------------------------------------------------"
"-- `on:do:` runs a protected block and, if it signals a matching           --"
"-- exception, runs the handler with the exception instance bound. A        --"
"-- handler that falls off its end makes `on:do:` yield the handler's last  --"
"-- value; the protected computation after `signal` is abandoned.           --"
"-- Expected output: recovered: disk full                                   --"

[ Error signal: 'disk full'. 'not reached' ]
  on: Error
  do: [ :ex | 'recovered: ' , ex messageText ].
