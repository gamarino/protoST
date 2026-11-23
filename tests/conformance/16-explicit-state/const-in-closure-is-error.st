"EXPECT-ERROR: cannot assign to the constant 'number' of Account inside a block: the block may run after construction (line 8)"
"Spec §6.2, M2, conformance 50: inside initialize, a real block may not
 assign a constant: the block may be kept and evaluated after construction.
 The inlined twin is const-in-inlined-block-allowed.st."
Object subclass: #Account instanceVariableNames: 'history' constantNames: 'number'.
Account >> initialize
  history := 0.
  [number := 1] value

3.
