"EXPECT: 20"
"§4: T3-a — overriding an imported class's method and calling `super` in the
override observably runs the module's original implementation. FastCounter
doubles every increment by invoking the imported Counter>>incrementBy: twice."
lib := Import from: '_extensible_counter'.
lib Counter subclass: #FastCounter instanceVariableNames: ''.

FastCounter >> incrementBy: n
  super incrementBy: n.
  super incrementBy: n.
  ^ self.

c := FastCounter newChild.
c initialize.
c incrementBy: 10.
c value.
