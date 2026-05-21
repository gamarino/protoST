"EXPECT: true"
"§13 stdlib: JSON parse: of each scalar type — string, integer, float,
true, false, null — yields the matching protoST value."
m := Import from: 'json'.
((m JSON parse: '"hi"') = 'hi')
  and: [ (m JSON parse: '42') = 42
  and: [ (m JSON parse: '3.5') = 3.5
  and: [ (m JSON parse: 'true') = true
  and: [ (m JSON parse: 'false') = false
  and: [ (m JSON parse: 'null') isNil ] ] ] ] ].
