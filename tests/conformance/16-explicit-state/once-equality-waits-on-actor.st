"EXPECT: #(true true 'refused' true)"
"Task 4 review ruling (spec §6.3, §10.4, M12): a write-once store on an actor
 whose installed value's = waits on a Future, here a reply that itself waits
 on a second actor. The store does not yield its worker: the nested = helps
 while it waits, and the store finishes on the same thread. With an equal
 value the store answers the installed value and does not hang; with an
 unequal one it signals M12 (ModificationForbidden), and the variable keeps
 the installed value."
Object subclass: #Relay instanceVariableNames: ''.
Relay >> pass: x
  ^ x

Object subclass: #Sink instanceVariableNames: 'relay'.
Sink >> relay: r
  relay := r
Sink >> echo: x
  ^ (relay pass: x) wait

Object subclass: #Waiter instanceVariableNames: 'sink answer'.
Waiter >> sink: s answer: a
  sink := s. answer := a
Waiter >> answer
  ^ answer
Waiter >> = other
  ^ (sink echo: other answer) wait

Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> trySetKey: x
  ^ [key := x. 'no error'] on: ModificationForbidden do: [:e | 'refused']
Holder >> key
  ^ key

| sink installed equal unequal h r1 r2 |
sink := Sink new.
sink relay: Relay new asActor.
sink := sink asActor.
installed := Waiter new sink: sink answer: true; yourself.
equal := Waiter new sink: sink answer: true; yourself.
unequal := Waiter new sink: sink answer: false; yourself.
h := Holder new asActor.
(h setKey: installed) wait.
r1 := (h setKey: equal) wait.
r2 := (h trySetKey: unequal) wait.
{r1 == installed. r1 ~~ equal. r2. (h key) wait == installed}
