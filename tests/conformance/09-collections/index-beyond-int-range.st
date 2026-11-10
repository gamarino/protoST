"EXPECT: #(#SubscriptOutOfBounds #SubscriptOutOfBounds #SubscriptOutOfBounds #SubscriptOutOfBounds #SubscriptOutOfBounds #SubscriptOutOfBounds #SubscriptOutOfBounds #SubscriptOutOfBounds)"
"An index past the range of a C int is out of bounds on a small collection:
 it signals SubscriptOutOfBounds and never wraps to a valid index (narrowed to
 an int, 2^32 + 1 would read element 1, 2^31 + 1 a negative index). The bounds
 check refuses it before any conversion; the conversion itself, to the int
 protoCore's lists take, goes through listIndex, which refuses an index past
 INT_MAX (src/runtime/ListIndex.h, tests/unit/test_list_index.cpp)."
| a oc s big huge kind |
a := #(10 20 30) copy.
oc := OrderedCollection withAll: #(10 20 30).
s := 'abc'.
big := (2 raisedTo: 32) + 1.
huge := (2 raisedTo: 31) + 1.
kind := [:block | [block value. #noError] on: Error do: [:e | e class name asSymbol]].
{ kind value: [a at: big].
  kind value: [a at: huge].
  kind value: [a at: big put: 0].
  kind value: [oc at: big].
  kind value: [oc at: huge].
  kind value: [oc at: big put: 0].
  kind value: [s at: big].
  kind value: [s at: huge] }
