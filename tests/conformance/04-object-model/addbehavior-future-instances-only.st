"EXPECT: old: doesNotUnderstand: mm"
"§4: T3-c — the documented `addBehavior:` limitation. An instance created
BEFORE the call froze its parent chain at construction; it does NOT gain the
mixin. The semantics are `future instances only` — the class and instances
created after the call respond; pre-existing instances keep the old chain."
Object subclass: #G.
Object subclass: #M.
M >> mm  ^ 'mixin'.
old := G newChild.
G addBehavior: M.
[ ^ 'unexpected: ', old mm ]
  on: Error
  do: [ :e | ^ 'old: ', e messageText ].
