"EXPECT: 2 100"
"A method's instance variable is not confused with a file-level variable of the
 same name that a top-level block captures: the method updates its own
 instance variable and leaves the file variable untouched."
Object subclass: #S instanceVariableNames: 'bad'.
S >> initialize  bad := 0. ^ self.
S >> hit  #(1 2) do: [ :x | bad := bad + 1 ]. ^ bad.

| s bad |
bad := 100.
s := S new.
#(1) do: [ :y | bad := bad + 0 ].
s hit printString , ' ' , bad printString.
