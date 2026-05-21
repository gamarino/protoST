"EXPECT: -1"
"§9.2: detect:ifNone: evaluates the fallback block when none matches"
#(1 2 3) detect: [ :e | e > 99 ] ifNone: [ 0 - 1 ].
