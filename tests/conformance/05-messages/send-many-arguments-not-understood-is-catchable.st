"EXPECT: a:b:c:d:e:f:g:h:i:"
"§5.4: a 9-keyword send that nothing understands signals MessageNotUnderstood,
 which on:do: catches like any other."
[3 a: 1 b: 2 c: 3 d: 4 e: 5 f: 6 g: 7 h: 8 i: 9]
  on: MessageNotUnderstood do: [:e | e message selector]
