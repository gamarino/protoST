"EXPECT: total-revenue-455"
"-- json_transform.st -----------------------------------------------------"
"-- A complete small program: a JSON-driven data transform. Parse a JSON    --"
"-- order document, walk its line items with the collection protocol,       --"
"-- compute a derived total, and emit the result.                           --"
"--                                                                        --"
"-- This is the everyday shape of a data pipeline: parse -> transform ->    --"
"-- aggregate, using `JSON parse:`, `Dictionary` access and `inject:into:`. --"

m := Import from: 'json'.

orders := m JSON parse: '
{
  "items": [
    { "name": "widget",  "qty": 3, "price": 25 },
    { "name": "gadget",  "qty": 2, "price": 100 },
    { "name": "sticker", "qty": 6, "price": 30 }
  ]
}'.

"Each item contributes qty * price; fold the line items into a grand total."
total := (orders at: 'items')
  inject: 0
  into: [ :acc :item |
    acc + ((item at: 'qty') * (item at: 'price')) ].

"3*25 + 2*100 + 6*30 = 75 + 200 + 180 = 455."
'total-revenue-' , total printString.
