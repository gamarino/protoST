"EXPECT: #(true false)"
"Spec §6.4, conformance 70: compareAndSet:from:to: installs the new value
 when the variable holds the expected one (nil matches unset) and answers
 whether it did; a CAS from a value not held answers false."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.

| a |
a := Account basicNew.
{a compareAndSet: #balance from: nil to: 0. a compareAndSet: #balance from: 5 to: 1}
