"EXPECT: done"
"A collection that contains itself prints without hanging."
oc := OrderedCollection new.
oc add: oc.
oc printString.
'done'.
