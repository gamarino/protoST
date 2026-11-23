"EXPECT: #(nil nil 3 7)"
"D19: classVariableNames: splits its names on any whitespace (spaces, a tab,
 a newline), as every declaration clause does; each class variable starts nil
 and is shared by the class and its instances."
Object subclass: #Cfg
  instanceVariableNames: ''
  classVariableNames: 'alpha	beta
 gamma'.
Cfg class >> setUp
  beta := 3. gamma := 7.
Cfg >> values
  ^ {alpha. Cfg peek. beta. gamma}
Cfg class >> peek
  ^ alpha

Cfg setUp.
Cfg new values
