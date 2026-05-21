"EXPECT: true"
"§13 stdlib: round-trip — parse: of stringify: recovers a non-trivial nested
structure with every scalar kind."
m := Import from: 'json'.
src := '{"id": 7, "ratio": 0.25, "tags": ["a", "b"], "ok": true, "extra": null}'.
once := m JSON parse: src.
twice := m JSON parse: (m JSON stringify: once).
((twice at: 'id') = 7)
  and: [ (twice at: 'ratio') = 0.25
  and: [ ((twice at: 'tags') at: 2) = 'b'
  and: [ (twice at: 'ok') = true
  and: [ (twice at: 'extra') isNil ] ] ] ].
