"EXPECT: 2 2 4 2 20 0 1"
"Dictionary newFrom: a collection of associations or another dictionary; addAll: and withAll: likewise."
d := Dictionary newFrom: {1->2. 3->4}.
e := Dictionary newFrom: d.
f := Dictionary new.
f addAll: {10->20}.
f addAll: (Dictionary newFrom: {30->40}).
d size printString , ' ' , (d at: 1) printString , ' ' , (e at: 3) printString , ' ' , f size printString , ' ' , (f at: 10) printString , ' ' , (Dictionary newFrom: #()) size printString , ' ' , (Dictionary withAll: {1->2}) size printString.
