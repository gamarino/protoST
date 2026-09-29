"EXPECT-ERROR: already defined"
"A method temporary may not reuse an instance variable's name."
Object subclass: #Foo instanceVariableNames: 'a'.
Foo >> m
  | a |
  a := 'temp'.
  ^ a.

Foo new m.
