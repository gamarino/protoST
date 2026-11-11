"EXPECT: true"
"UDP send: refuses an Array of Integers since 0.10.0 (D45), naming ByteArray
 as the replacement; nothing is sent."
Import from: 'net'.
| tx r |
tx := UDPSocket bindTo: 0.
r := [tx send: #(1 0 2) to: '127.0.0.1' port: 9. false]
  on: Error do: [:e | (e messageText includesSubstring: 'an Array is not bytes')
    and: [e messageText includesSubstring: 'ByteArray']].
tx close.
r.
