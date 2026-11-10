"EXPECT-ERROR: SubscriptOutOfBounds"
"copyReplaceFrom:to:with: checks its range like copyFrom:to: (1 <= start <= size + 1, start - 1 <= stop <= size): a stop past the end signals SubscriptOutOfBounds instead of answering a truncated value."
#[1 2] copyReplaceFrom: 2 to: 100 with: #[9].
