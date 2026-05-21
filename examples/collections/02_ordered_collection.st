"EXPECT: 5"
"-- 02_ordered_collection.st -----------------------------------------------"
"-- OrderedCollection is the growable, indexed workhorse collection. It is  --"
"-- built up with `add:` (here cascaded onto one receiver), then trimmed    --"
"-- from both ends with `removeFirst` / `removeLast`.                       --"
"-- Expected output: 5  (the remaining size after building 7 and removing 2)--"

oc := OrderedCollection new.
oc add: 10; add: 20; add: 30; add: 40; add: 50; add: 60; add: 70.
oc removeFirst.
oc removeLast.
oc size.
