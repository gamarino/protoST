"EXPECT-ERROR"
"§9.2: detect: signals an Error when no element matches"
#(1 2 3) detect: [ :e | e > 99 ].
