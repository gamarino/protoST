"EXPECT: #(true true true true 'R2F')"
"D52, review round 2 (R2-L3): a metaclass has exactly one instance, its
 class, made with it by subclass:. As Pharo's Metaclass >> new, `Foo class
 new` (and basicNew, newChild) is an error, and so is `Metaclass new`; the
 class itself still makes instances."
Object subclass: #R2F.
| refused |
refused := [:b | [b value. false] on: Error do: [:e | e return: (e messageText includesSubstring: 'one instance')]].
{ refused value: [R2F class new].
  refused value: [R2F class basicNew].
  refused value: [R2F class newChild].
  refused value: [Metaclass new].
  R2F new class name }
