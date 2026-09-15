"EXPECT: 24000"
"D26 regression: Float literals (heap-allocated doubles) inside a method that"
"runs on actor worker threads. They used to be allocated on the main thread's"
"context from every worker, which corrupted memory on every run."
"8 actors x 1000 rounds; each send answers 3."
Object subclass: #M.
M >> lit  ^ (2.5 + 0.5) truncated.
ms := OrderedCollection new.
1 to: 8 do: [ :i | ms add: M new asActor ].
sum := 0.
futs := nil.
1 to: 1000 do: [ :k | futs := OrderedCollection new. ms do: [ :a | futs add: a lit ]. futs do: [ :f | sum := sum + f wait ] ].
sum.
