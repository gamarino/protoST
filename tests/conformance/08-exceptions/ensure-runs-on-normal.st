"EXPECT: 1"
"§8.5: ensure: cleanup runs when the block completes normally"
flag := 0.
[ 42 ] ensure: [ flag := 1 ].
flag.
