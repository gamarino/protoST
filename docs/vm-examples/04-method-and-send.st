Object subclass: #Greeter
  instanceVariableNames: 'name'.

Greeter >> name: aString
  name := aString.

Greeter >> greeting
  ^ 'Hello, ', name.

(Greeter new name: 'Ada') greeting printNl.
