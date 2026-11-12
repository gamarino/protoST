"EXPECT: 2"
"§10.8 (S39): a cell made with new holds nil, so swap: applies its block to
nil and installs the result; it used to retry for ever, because the
compare-and-swap expected an explicit nil where the cell held nothing."
a := Atom new.
a swap: [ :v | v isNil ifTrue: [ 1 ] ifFalse: [ v + 1 ] ].
a swap: [ :v | v + 1 ].
a value.
