"EXPECT: 3"
"class-variable storage is shared: class-side mutation visible to instances"
Object subclass: #C instanceVariableNames: '' classVariableNames: 'tally'.
C class >> init  tally := 0.
C class >> bump  tally := tally + 1.
C >> read  ^ tally.
C init.
C bump.
C bump.
C bump.
C new read.
