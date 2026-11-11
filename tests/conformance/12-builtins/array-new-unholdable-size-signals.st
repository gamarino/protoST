"EXPECT: #('beyond protoCore''s list limit' 'more than the heap limit' 'more than the heap limit' true true 9 'ok')"
"Array new: with a size that can never be held signals a catchable Error before anything is allocated (review R3-2): beyond protoCore's list size (2^32 - 1), or more elements than the heap ceiling has cells. protoCore counts cells in an int, so 2^31 is beyond the ceiling on every machine; the test allocates nothing large. A LargeInteger size signals too, and the program goes on."
| try key |
try := [ :blk | [ blk value. 'no error' ] on: Error do: [ :e | e messageText ] ].
key := [ :text :sub | (text includesSubstring: sub) ifTrue: [ sub ] ifFalse: [ text ] ].
{ key value: (try value: [ Array new: 4294967296 ]) value: 'beyond protoCore''s list limit'.
  key value: (try value: [ Array new: 2147483648 ]) value: 'more than the heap limit'.
  key value: (try value: [ Array new: 2147483648 withAll: 0 ]) value: 'more than the heap limit'.
  (try value: [ Array new: (2 raisedTo: 70) ]) ~= 'no error'.
  ([ Array new: 4294967296 ] on: Error do: [ :e | e return: true ]).
  (Array new: 9) size.
  'ok' }
