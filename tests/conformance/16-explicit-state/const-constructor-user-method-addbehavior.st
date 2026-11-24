"EXPECT: #('user tag:base:' 'tag:base: is the generated constructor of Grown and cannot be redefined; give your constructor another name' 'user tag:base:' true 2 'helper')"
"Spec §6.2, M9, Task 6 ruling I1: a class method installed while its
 selector was not a constructor is never overwritten. addBehavior: whose
 new constants would make that selector the generated constructor is
 refused with M9 before anything changes: the class keeps its method, its
 constructor and its other class methods."
(Object yourself) subclass: #Grown instanceVariableNames: '' constantNames: 'base' writeOnceNames: '' atomicNames: ''.
(Object yourself) subclass: #Tagged instanceVariableNames: '' constantNames: 'tag' writeOnceNames: '' atomicNames: ''.
Grown class >> tag: t base: b
  ^ 'user tag:base:'
Grown class >> helper
  ^ 'helper'
Grown >> base
  ^ base

| before refused g |
before := Grown tag: 1 base: 2.
refused := [Grown addBehavior: Tagged. 'composed'] on: Error do: [:e | e messageText].
g := Grown base: 2.
{before. refused. Grown tag: 1 base: 2. g class == Grown. g base. Grown helper}
