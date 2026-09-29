"EXPECT: 1 true found"
"Set and Dictionary use a class's own = and hash."
Object subclass: #P instanceVariableNames: 'k'.
P >> k: v
  k := v.
  ^ self.

P >> k
  ^ k.

P >> = other
  ^ (other isKindOf: P) and: [k = other k].

P >> hash
  ^ k hash.

s := Set new.
s add: (P new k: 1); add: (P new k: 1).
d := Dictionary new.
d at: (P new k: 2) put: 'found'.
s size printString , ' ' , (s includes: (P new k: 1)) printString , ' ' , (d at: (P new k: 2) ifAbsent: ['missing']).
