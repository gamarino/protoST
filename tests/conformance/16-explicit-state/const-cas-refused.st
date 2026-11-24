"EXPECT: #('compareAndSet:from:to: cannot write the constant ''number'' of an Account; assign it in initialize' 'update:with: cannot write the constant ''number'' of an Account; assign it in initialize' nil 1)"
"Spec §6.5, M5, conformance 54: neither CAS message writes a constant. Both
 signal ModificationForbidden in place, before anything is read or written:
 the update block never runs, and the constant keeps its value."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number'.
Account >> initialize
  number := 1
Account >> number
  ^ number

| a ran r1 r2 |
a := Account new.
r1 := [a compareAndSet: #number from: 1 to: 2] on: ModificationForbidden do: [:e | e messageText].
r2 := [a update: #number with: [:old | ran := true. old + 1]]
  on: ModificationForbidden do: [:e | e messageText].
{r1. r2. ran. a number}
