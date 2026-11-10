"EXPECT: SOB SOB SOB SOB SOB SOB SOB | Error Error Error Error Error | 4 2 9 | Interval>>at: index 1 out of range 1..0"
"The indexing methods written in Smalltalk follow the rule of the indexing
 primitives (src/primitives/IndexArg.h): an Integer index out of range,
 however large, signals SubscriptOutOfBounds; an index that is not an Integer
 (a Float, a Character) signals Error. Covered: NumericInterval>>at: (every
 empty, single-element, inexact-stride or Float interval, and through it
 first and last), OrderedCollection>>removeAt: and add:beforeIndex:, and
 ByteArray>>copyReplaceFrom:to:with:. NumericInterval>>at: used to signal a
 plain Error 'Interval>>at:: index ...' and to accept a Float index."
| kind oc out |
kind := [:block | [[block value. 'ok'] on: SubscriptOutOfBounds do: [:e | 'SOB']]
  on: Error do: [:e | 'Error']].
oc := [OrderedCollection withAll: #(1 2 3)].
out := WriteStream on: String new.
{ [(1 to: 0) at: 1].
  [(1 to: 0) first].
  [(1 to: 9 by: 3) at: 9].
  [(1 to: 5 by: 0.5) at: (2 raisedTo: 64)].
  [oc value removeAt: 4].
  [oc value removeAt: (2 raisedTo: 70) negated].
  [oc value add: 9 beforeIndex: 5] } do: [:b | out nextPutAll: (kind value: b); space].
out nextPutAll: '| '.
{ [(1 to: 5 by: 0.5) at: 1.0].
  [(1 to: 9 by: 3) at: (Character value: 2)].
  [oc value removeAt: 1.5].
  [oc value add: 9 beforeIndex: $a].
  [#[1 2 3] copyReplaceFrom: 1.0 to: 1 with: #[9]] } do: [:b | out nextPutAll: (kind value: b); space].
out nextPutAll: '| ';
  print: ((1 to: 9 by: 3) at: 2); space;
  print: (oc value removeAt: 2); space;
  print: ((oc value add: 9 beforeIndex: 4; yourself) last); nextPutAll: ' | ';
  nextPutAll: ([(1 to: 0) at: 1] on: SubscriptOutOfBounds do: [:e | e messageText]).
out contents
