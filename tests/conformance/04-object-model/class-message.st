"EXPECT: true"
"class is an ordinary unary message, also at the start of a statement; only
 the exact form  Name class >> selector  declares a class-side method."
x := 3 class.
y := OrderedCollection new class.
(x == SmallInteger) & (y == OrderedCollection) & ('a' class == String).
