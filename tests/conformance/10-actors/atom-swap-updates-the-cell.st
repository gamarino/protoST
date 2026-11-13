"EXPECT: 8 seam true"
"§10.8 (S38): an override of value changes what a client reads, not what the
atomic operations work on. swap: and value:ifCurrent: compare against the
cell's own snapshot -- the value the compare-and-swap checks -- so swap:
applies its block to the cell's 7, and the CAS expects the cell's 8."
Atom subclass: #SeamAtom.

SeamAtom >> value
  ^ 'seam'.

a := SeamAtom on: 7.
s := a swap: [ :v | v + 1 ].
s printString , ' ' , a value , ' ' , (a value: 9 ifCurrent: 8) printString.
