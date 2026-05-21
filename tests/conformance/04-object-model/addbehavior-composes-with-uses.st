"EXPECT: A/B/own/D"
"§4: T3-c — `addBehavior:` composes with `uses:`. `C` is defined with two
mixins (`A`, `B`) and its own method, then given a further behaviour `D` at
runtime. An instance answers a method from every source."
Object subclass: #A.
A >> fromA  ^ 'A'.
Object subclass: #B.
B >> fromB  ^ 'B'.
Object subclass: #C uses: { A. B }.
C >> own  ^ 'own'.
Object subclass: #D.
D >> fromD  ^ 'D'.
C addBehavior: D.
^ (C newChild) fromA, '/', (C newChild) fromB, '/',
  (C newChild) own, '/', (C newChild) fromD.
