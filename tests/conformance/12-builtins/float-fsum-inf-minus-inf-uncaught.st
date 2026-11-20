"EXPECT-ERROR: fsum: -inf + inf"
"Both infinities among the terms signal an Error (CPython's ValueError)."
Float fsumOfProducts: { Float infinity. Float infinity } with: #(1.0 -1.0).
