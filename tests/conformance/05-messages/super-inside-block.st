"EXPECT: 6"
"super may be sent from inside a block within a method."
Object subclass: #Base.
Base >> double: n
  ^ n * 2.

Base subclass: #Derived.
Derived >> double: n
  ^ n + 1000.

Derived >> sumDoubled: aCollection
  ^ aCollection inject: 0 into: [:acc :each | acc + (super double: each)].

Derived new sumDoubled: #(1 2).
