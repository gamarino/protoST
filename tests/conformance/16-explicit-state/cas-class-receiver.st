"EXPECT: #(true 1 2 'Account has no instance variable ''balance''')"
"Pre-flight rulings C10 and C7: a class receiver's variables are its
 class-instance variables, which are always plain, so both CAS messages
 work on them; an instance-side name is not one of the class's variables,
 and M8 names the class by its bare name."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.
Account class instanceVariableNames: 'registry'.
Account class >> registry
  ^ registry

| r |
r := [Account compareAndSet: #balance from: nil to: 1] on: Error do: [:e | e messageText].
{Account compareAndSet: #registry from: nil to: 1.
 Account registry.
 Account update: #registry with: [:old | old + 1].
 r}
