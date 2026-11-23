"EXPECT-ERROR: cannot assign to the atomic variable 'balance' of Account: use update:with: or compareAndSet:from:to: (line 7)"
"Spec §6.4, M4, conformance 68: := to an atomic variable is a compile
 error anywhere, initialize included; it is written only by update:with: and
 compareAndSet:from:to:."
Object subclass: #Account instanceVariableNames: 'history' atomicNames: 'balance'.
Account >> reset
  balance := 0

3.
