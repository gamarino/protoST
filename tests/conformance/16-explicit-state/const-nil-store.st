"EXPECT: #(7 'the constant ''number'' of an Acc is already set; nothing was written' 7)"
"Spec §6.2: storing nil into an unset constant succeeds and changes nothing;
 the first non-nil store sets it; storing nil into a set constant is M3 and
 leaves the value. basicNew skips the end-of-construction check, so the
 constant can stay unset while initialize is sent by hand."
Object subclass: #Acc instanceVariableNames: 'pending' constantNames: 'number'.
Acc >> pending: x
  pending := x
Acc >> initialize
  number := pending
Acc >> number
  ^ number

| a r |
a := Acc basicNew.
a initialize.
a pending: 7.
a initialize.
r := [a pending: nil. a initialize. 'no error'] on: ModificationForbidden do: [:e | e messageText].
{a number. r. a number}
