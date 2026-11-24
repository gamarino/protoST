"EXPECT-ERROR: cannot assign to the constant 'number' of Account outside initialize (line 7)"
"Spec §6.2, M1, conformance 49: a constant is assigned only in the
 instance-side initialize; any other method that assigns it is a compile
 error."
Object subclass: #Account instanceVariableNames: 'history' constantNames: 'number'.
Account >> setNumber: n
  number := n

3.
