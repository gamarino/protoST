"EXPECT: true true Random Random true"
"Random new seed: n re-seeds an existing generator (the same sequence as Random seed: n); the class-side seed: still answers a new Random."
r := Random new.
r seed: 42.
x := r next.
r next.
r seed: 42.
t := (x = (Random seed: 42) next) printString , ' ' , (r next = x) printString , ' ' , (Random seed: 1) class name , ' ' , (Random new seed: 7) class name , ' ' , ((Random new seed: 7) next = (Random seed: 7) next) printString.
t.
