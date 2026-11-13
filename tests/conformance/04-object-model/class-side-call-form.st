"EXPECT: #(2 20 7 70 14 true true)"
"§4.7, K14, D52: a call-form method can be defined on the class side, and one
 call-form selector can name a method on each side; the class finds its
 class-side method, an instance its instance-side one (the SEND_CALL path
 looks up from the receiver's method holder, as SEND does)."
Object subclass: #K14CF.
K14CF class >> make(x)  ^ x + 1.
K14CF >> make(x)  ^ x * 10.
K14CF class >> scaled(x, by = 1)  ^ x * by.
K14CF >> scaled(x, by = 1)  ^ x * by * 10.
| r inst |
r := OrderedCollection new.
inst := K14CF new.
r add: K14CF make(1).
r add: inst make(2).
r add: K14CF scaled(7).
r add: inst scaled(7).
r add: K14CF scaled(7, by = 2).
r add: (K14CF respondsTo: #make).
r add: (inst respondsTo: #scaled).
r asArray
