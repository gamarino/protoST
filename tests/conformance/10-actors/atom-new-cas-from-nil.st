"EXPECT: true 5 false"
"§10.8 (S39): value:ifCurrent: on a cell made with new matches an expected
nil, as an unset instance variable matches nil in setInstVar:from:to:; a
second CAS from nil then fails, since the cell holds 5."
a := Atom new.
r := a value: 5 ifCurrent: nil.
r printString , ' ' , a value printString , ' ' , (a value: 6 ifCurrent: nil) printString.
