"EXPECT: true"
"Exponent and radix number literals (0.4.0). 1.0e-10 used to evaluate to -7.28."
(1.0e-10 < 0.001) & (1.0e-10 > 0) & (1e3 = 1000) & (2.5e-3 = 0.0025) & (16rFF = 255) & (2r1010 = 10) & (1.5e2 = 150.0).
