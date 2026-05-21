"EXPECT: hypotenuse-5"
"-- 02_math.st -------------------------------------------------------------"
"-- The mathematical protocol is bound on `Number` — always available, no   --"
"-- `Import` needed. `squared`, `sqrt` and `rounded` compute the hypotenuse --"
"-- of a 3-4-5 right triangle; `Float pi` is one of the class-side          --"
"-- constants on `Float`.                                                   --"
"-- Expected output: hypotenuse-5                                           --"

a := 3.
b := 4.
hyp := (a squared + b squared) sqrt.

"sqrt answers a Float (5.0); rounded brings it back to an integer."
'hypotenuse-' , hyp rounded printString.
