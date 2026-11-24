"EXPECT: #('an Account has no instance variable ''zork''' 'an Account has no instance variable ''zork''' nil 'compareAndSet:from:to: expects a String or Symbol, not 3' 'update:with: expects a String or Symbol, not nil' 'a SmallInteger has no instance variable ''zork''' 'nil has no instance variable ''zork''')"
"Spec §6.4, M8, conformance 75: both CAS messages refuse a name the receiver
 does not declare, with Error, before anything is written: no attribute is
 created and the block never runs. A name that is not a String or Symbol is
 refused too, and so is any name sent to a receiver without instance variables."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.

| a ran r1 r2 r3 r4 r5 r6 |
a := Account basicNew.
r1 := [a compareAndSet: #zork from: nil to: 1] on: Error do: [:e | e messageText].
r2 := [a update: 'zork' with: [:old | ran := true. 1]] on: Error do: [:e | e messageText].
r3 := [a compareAndSet: 3 from: nil to: 1] on: Error do: [:e | e messageText].
r4 := [a update: nil with: [:old | ran := true. 1]] on: Error do: [:e | e messageText].
r5 := [3 compareAndSet: #zork from: nil to: 1] on: Error do: [:e | e messageText].
r6 := [nil update: #zork with: [:old | ran := true. 1]] on: Error do: [:e | e messageText].
{r1. r2. (a instVarNamed: #zork) ifNil: [ran]. r3. r4. r5. r6}
