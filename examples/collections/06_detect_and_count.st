"EXPECT: prime-count-25"
"-- 06_detect_and_count.st -------------------------------------------------"
"-- `count:` answers how many elements satisfy a block; `detect:ifNone:`    --"
"-- finds the first match (or runs the fallback). Here we count the primes  --"
"-- below 100 with a trial-division test expressed as a block.              --"
"-- Expected output: prime-count-25                                         --"

Object subclass: #Primality.
Primality >> isPrime: n
  | prime |
  n < 2 ifTrue: [ ^ false ].
  prime := true.
  2 to: n - 1 do: [ :d |
    ((n \\ d) = 0) ifTrue: [ prime := false ] ].
  ^ prime.

checker := Primality new.
primeCount := (2 to: 99) count: [ :n | checker isPrime: n ].
'prime-count-' , primeCount printString.
