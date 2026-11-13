"EXPECT: #('i2' 'c1' 'i2' 'c2' 'i2' 'c2')"
"§4.7, K14: the two sides of a class are two slots. Redefining the
 instance-side method leaves the class-side one alone, and the other way
 round; redefining a side twice keeps only its latest method."
Object subclass: #K14Re.
K14Re >> x  ^ 'i1'.
K14Re class >> x  ^ 'c1'.
K14Re >> x  ^ 'i2'.
| a |
a := OrderedCollection new.
a add: K14Re new x; add: K14Re x.
K14Re class >> x  ^ 'c2'.
a add: K14Re new x; add: K14Re x.
K14Re class >> x  ^ 'c2'.
a add: K14Re new x; add: K14Re x.
a asArray
