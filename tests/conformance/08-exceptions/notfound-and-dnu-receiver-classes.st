"EXPECT: nf"
"detect: with no match signals NotFound; a message not understood names LargeInteger and Symbol receivers correctly."
| e1 e2 |
[(2 raisedTo: 70) foo] on: MessageNotUnderstood do: [:e | e1 := e messageText].
[#abcdefghij foo] on: MessageNotUnderstood do: [:e | e2 := e messageText].
(e1 includesSubstring: 'LargeInteger') & (e2 includesSubstring: 'Symbol') ifFalse: [Error signal: e1 , e2].
[#(1 2) detect: [:x | x > 5]] on: NotFound do: [:e | 'nf']
