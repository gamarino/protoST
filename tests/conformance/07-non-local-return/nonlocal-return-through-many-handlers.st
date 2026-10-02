"EXPECT: found cleanups:480"
"§7: a ^ in a block returns from its home method through every frame in
 between: here 480 levels, each with an on:do: (whose class does not match)
 and an ensure:, about 960 nested engines. Every ensure: block runs on the
 way out."
Object subclass: #Walker instanceVariableNames: 'cleanups'.
Walker >> init
  cleanups := 0.
Walker >> find
  self walk: 480 exit: [^ 'found'].
  ^ 'not found'.
Walker >> walk: n exit: aBlock
  n = 0 ifTrue: [aBlock value].
  ^ [[self walk: n - 1 exit: aBlock] on: ZeroDivide do: [:e | 0]]
      ensure: [cleanups := cleanups + 1].
Walker >> cleanups
  ^ cleanups.
| w f |
w := Walker new init.
f := w find.
f , ' cleanups:' , w cleanups printString.
