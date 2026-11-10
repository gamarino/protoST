"EXPECT: #('Future class>>after: the milliseconds: an Integer was expected' 'Future class>>after: the milliseconds is out of range' 'Future class>>after: the milliseconds must not be negative, not -1' 'Future>>within: the milliseconds is out of range')"
"§10 Future after: and within: take a non-negative SmallInteger of
 milliseconds. A LargeInteger is an Integer, so it is out of range, not a type
 error; each text names the selector once (no doubled colon)."
| msg |
msg := [:block | [block value. 'no error'] on: Error do: [:e | e messageText]].
{ msg value: [Future after: 'x'].
  msg value: [Future after: (2 raisedTo: 70)].
  msg value: [Future after: -1].
  msg value: [Future new within: (2 raisedTo: 70)] } printString
