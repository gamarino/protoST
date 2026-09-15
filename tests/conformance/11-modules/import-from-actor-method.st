"EXPECT: 5040"
"S6 regression (§11.2): `Import from:` inside a method that runs on actor"
"worker threads. The module provider found its runtime through a thread-local"
"pointer set only on the main thread, so every import on a worker answered"
"`module not found`. 8 actors x 70 rounds; the i-th actor imports the module"
"and answers (Doubler of: i), so each round sums 2 * (1 + ... + 8) = 72."
Object subclass: #Importer.
Importer >> doubled: n
  | m |
  m := Import from: '_actor_value_mod'.
  ^ m Doubler of: n.
ms := OrderedCollection new.
1 to: 8 do: [ :i | ms add: Importer new asActor ].
sum := 0.
1 to: 70 do: [ :k |
  | futs i |
  futs := OrderedCollection new.
  i := 0.
  ms do: [ :a | i := i + 1. futs add: (a doubled: i) ].
  futs do: [ :f | sum := sum + f wait ] ].
sum.
