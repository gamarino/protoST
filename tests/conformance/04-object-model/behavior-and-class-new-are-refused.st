"EXPECT: #(true true true true true true 'R3B' true)"
"D52, review round 3 (R3-L1): Behavior and Class are made only by
 subclass:, which names the class and gives it its metaclass. protoST has no
 anonymous classes (Pharo's `Class new` makes one), so `Behavior new` and
 `Class new` (and basicNew, newChild) are errors, as `Metaclass new` is; an
 ordinary class still makes instances, and Metaclass keeps its own message."
Object subclass: #R3B.
| refused |
refused := [:b | [b value. false] on: Error do: [:e | e return: (e messageText includesSubstring: 'anonymous classes')]].
{ refused value: [Behavior new].
  refused value: [Behavior basicNew].
  refused value: [Behavior newChild].
  refused value: [Class new].
  refused value: [Class basicNew].
  refused value: [Class newChild].
  R3B new class name.
  [Metaclass new. false] on: Error do: [:e | e return: (e messageText includesSubstring: 'one instance')] }
