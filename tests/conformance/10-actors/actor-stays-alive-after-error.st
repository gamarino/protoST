"EXPECT: 5"
"§10.7: after an error in one message, the actor stays alive for the next"
Object subclass: #Resilient.
Resilient >> boom
  Error signal: 'x'.
Resilient >> fine
  ^ 5.
a := Resilient new asActor.
a boom.
(a fine) wait.
