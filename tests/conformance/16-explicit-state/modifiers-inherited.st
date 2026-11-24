"EXPECT-ERROR: cannot assign to the atomic variable 'balance' of Sub: use update:with: or compareAndSet:from:to: (line 8)"
"Spec §6.1, §6.4, M4, conformance 77: a subclass inherits its superclass's
 modifiers; assigning the inherited atomic variable is refused, naming the
 class of the method."
Object subclass: #Account instanceVariableNames: 'history' atomicNames: 'balance'.
Account subclass: #Sub instanceVariableNames: 'extra'.
Sub >> f
  balance := 1

3.
