"EXPECT: 432000"
"D26 regression: a string literal longer than six bytes (a heap string, not an"
"inline one) inside a method that runs on actor worker threads. The literal"
"used to be allocated on the main thread's context from every worker, which"
"corrupted memory on every run. 8 actors x 1000 rounds; each send answers 54."
Object subclass: #M.
M >> lit  ^ 'a fairly long string literal for the materialize probe' size.
ms := OrderedCollection new.
1 to: 8 do: [ :i | ms add: M new asActor ].
sum := 0.
futs := nil.
1 to: 1000 do: [ :k | futs := OrderedCollection new. ms do: [ :a | futs add: a lit ]. futs do: [ :f | sum := sum + f wait ] ].
sum.
