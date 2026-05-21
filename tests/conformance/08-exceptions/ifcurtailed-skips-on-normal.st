"EXPECT: 0"
"§8.5: ifCurtailed: cleanup does NOT run on normal completion"
flag := 0.
[ 42 ] ifCurtailed: [ flag := 1 ].
flag.
