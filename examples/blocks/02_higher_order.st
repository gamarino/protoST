"EXPECT: 256"
"-- 02_higher_order.st -----------------------------------------------------"
"-- Higher-order patterns: blocks are first-class values, so they can be    --"
"-- passed to and returned from methods. `twice:` composes a one-argument   --"
"-- block with itself; applying the squaring block twice computes           --"
"-- ((4*4) * (4*4)) = 256.                                                  --"
"-- Expected output: 256                                                    --"

Object subclass: #Combinators.

"Return a block that applies `aBlock` twice."
Combinators >> twice: aBlock
  ^ [ :x | aBlock value: (aBlock value: x) ].

square := [ :n | n * n ].
squareTwice := Combinators new twice: square.
squareTwice value: 4.
