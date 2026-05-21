"EXPECT: 220"
"-- 01_iteration_protocol.st -----------------------------------------------"
"-- The shared iteration protocol over an Array: `select:` keeps the even   --"
"-- numbers, `collect:` squares them, `inject:into:` folds them to a sum.   --"
"-- These messages are defined once on Collection and inherited by every    --"
"-- collection class.                                                       --"
"-- Expected output: 220  (2*2 + 4*4 + 6*6 + 8*8 + 10*10)                   --"

evens   := #(1 2 3 4 5 6 7 8 9 10) select: [ :n | n isEven ].
squares := evens collect: [ :n | n * n ].
squares inject: 0 into: [ :acc :each | acc + each ].
