"EXPECT: 3"
"§10.1-10.3: asActor wraps an object; an async send returns a Future; wait settles it"
Object subclass: #Counter
  instanceVariableNames: 'value'.
Counter >> initialize
  value := 0.
Counter >> increment
  value := value + 1.
Counter >> value
  ^ value.
c := Counter new.
c initialize.
a := c asActor.
a increment.
a increment.
a increment.
(a value) wait.
