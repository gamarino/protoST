"EXPECT: 14"
"-- 01_early_return.st -----------------------------------------------------"
"-- Non-local return: a `^` inside a block returns from the block's HOME    --"
"-- METHOD, abandoning any intervening computation. `firstOver:` walks a    --"
"-- collection and `^`s the first element over a threshold straight out of  --"
"-- the `do:` loop — a hand-written `detect:`.                              --"
"-- Expected output: 14                                                     --"

Object subclass: #Search.

Search >> firstOver: aCollection threshold: limit
  aCollection do: [ :each |
    each > limit ifTrue: [ ^ each ] ].
  "Reached only if nothing matched — the loop did not return early."
  ^ -1.

Search new firstOver: #(3 8 14 21 5) threshold: 10.
