"EXPECT: #('owner' 'Account')"
"Spec §6.9, conformance 98: an actor's method reads an unset constant. The
 UnsetConstantError rejects the message's Future, and wait re-signals it in
 the waiter, so on: UnsetConstantError do: around the wait catches it, with
 its accessors: the constant's name and the object whose constant was read."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> read
  ^ owner

| actor |
actor := Account basicNew asActor.
[(actor read) wait] on: UnsetConstantError do: [:e | {e variableName. e receiver class name}]
