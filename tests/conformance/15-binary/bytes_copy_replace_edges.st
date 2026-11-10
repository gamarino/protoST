"EXPECT: #[9 1 2] #[1 2 9] #[9 2] #[1] true true true"
"copyReplaceFrom:to:with: at its edges: insertion before the first and after the last byte, replacement of the first byte, deletion; and SubscriptOutOfBounds for a start of 0, a start past size + 1 and a stop before start - 1."
b := #[1 2].
oob := [:blk | [blk value. false] on: SubscriptOutOfBounds do: [:e | true]].
(b copyReplaceFrom: 1 to: 0 with: #[9]) printString , ' ' ,
  (b copyReplaceFrom: 3 to: 2 with: #[9]) printString , ' ' ,
  (b copyReplaceFrom: 1 to: 1 with: #[9]) printString , ' ' ,
  (b copyReplaceFrom: 2 to: 2 with: #[]) printString , ' ' ,
  (oob value: [b copyReplaceFrom: 0 to: 1 with: #[9]]) printString , ' ' ,
  (oob value: [b copyReplaceFrom: 4 to: 3 with: #[9]]) printString , ' ' ,
  (oob value: [b copyReplaceFrom: 2 to: 0 with: #[9]]) printString.
