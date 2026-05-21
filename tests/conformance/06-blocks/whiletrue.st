"EXPECT: 11"
"§6.4: whileTrue: loops while the receiver block is true"
i := 1.
[ i <= 10 ] whileTrue: [ i := i + 1 ].
i.
