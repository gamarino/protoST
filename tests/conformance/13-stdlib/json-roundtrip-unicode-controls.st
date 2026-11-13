"EXPECT: true"
"§13 stdlib: control characters are written as escapes and a string with controls, non-BMP and BMP text round-trips."
m := Import from: 'json'.
| o w | o := 'a', 1 asCharacter asString, 31 asCharacter asString, 127 asCharacter asString, 233 asCharacter asString, 128512 asCharacter asString, 9 asCharacter asString. w := m JSON stringify: o. ((w includesSubstring: '\u0001') and: [ (w includesSubstring: '\u001f') and: [ (m JSON parse: w) = o ] ]).
