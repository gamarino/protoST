"EXPECT: 13 SubscriptOutOfBounds, 0 other; Array>>at: index 18446744073709551616 out of range 1..3"
"An index beyond a C long long (2^63 or more, or a negative LargeInteger) is
 out of the bounds of every collection: it signals the same
 SubscriptOutOfBounds as any other out-of-range index, with the index's exact
 digits in the message. Every indexing primitive reads its index through one
 shared reader (src/primitives/IndexArg.h); reading it with asLong raised
 ArithmeticError instead."
| a oc s b iv big kinds sub text |
a := #(10 20 30) copy.
oc := OrderedCollection withAll: #(10 20 30).
s := 'abc'.
b := #[1 2 3 4].
iv := 1 to: 5.
big := {2 raisedTo: 63. 2 raisedTo: 64. 2 raisedTo: 100. (2 raisedTo: 70) negated}.
kinds := OrderedCollection new.
sub := [:block | kinds add: ([block value. #noError] on: Error do: [:e | e class name asSymbol])].
big do: [:i | sub value: [a at: i]].
sub value: [a at: (2 raisedTo: 64) put: 0].
sub value: [oc at: (2 raisedTo: 64)].
sub value: [oc at: (2 raisedTo: 64) put: 0].
sub value: [iv at: (2 raisedTo: 64)].
sub value: [s at: (2 raisedTo: 64)].
sub value: [s copyFrom: 1 to: (2 raisedTo: 64)].
sub value: [b at: (2 raisedTo: 64)].
sub value: [b copyFrom: 1 to: (2 raisedTo: 64)].
sub value: [b unsignedLongAt: (2 raisedTo: 64) bigEndian: true].
text := [a at: (2 raisedTo: 64)] on: SubscriptOutOfBounds do: [:e | e messageText].
(kinds select: [:k | k = #SubscriptOutOfBounds]) size printString, ' SubscriptOutOfBounds, ',
  (kinds reject: [:k | k = #SubscriptOutOfBounds]) size printString, ' other; ', text
