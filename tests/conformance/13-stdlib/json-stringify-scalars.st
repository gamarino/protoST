"EXPECT: true"
"§13 stdlib: JSON stringify: of each scalar type."
m := Import from: 'json'.
((m JSON stringify: 42) = '42')
  and: [ (m JSON stringify: true) = 'true'
  and: [ (m JSON stringify: false) = 'false'
  and: [ (m JSON stringify: nil) = 'null'
  and: [ (m JSON stringify: 'hi') = ('"' , 'hi' , '"') ] ] ] ].
