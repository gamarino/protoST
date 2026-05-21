"EXPECT: 42"
"§10.3: Future wait blocks until settled and returns the value"
Object subclass: #Producer.
Producer >> answer
  ^ 42.
a := Producer new asActor.
(a answer) wait.
