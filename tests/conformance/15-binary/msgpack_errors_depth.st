"EXPECT-ERROR: deeper than 512"
"An acyclic structure nested beyond protoIO's depth limit signals Error on encode, as decode refuses it."
| a |
a := Array new.
600 timesRepeat: [a := Array with: a].
MessagePack encode: a.
