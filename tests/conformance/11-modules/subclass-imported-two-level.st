"EXPECT: 20"
"§4: T3-a — a two-level chain: a subclass of a subclass of a module class,
each level overriding incrementBy: and chaining through `super`. TurboCounter
doubles FastCounter, which doubles the imported Counter: 5 * 2 * 2 = 20."
lib := Import from: '_extensible_counter'.
lib Counter subclass: #FastCounter instanceVariableNames: ''.

FastCounter >> incrementBy: n
  super incrementBy: n.
  super incrementBy: n.
  ^ self.

FastCounter subclass: #TurboCounter instanceVariableNames: ''.

TurboCounter >> incrementBy: n
  super incrementBy: n.
  super incrementBy: n.
  ^ self.

t := TurboCounter newChild.
t initialize.
t incrementBy: 5.
t value.
