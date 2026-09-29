"EXPECT: #(5 10 1)"
"A block parameter that reuses the name of a captured method temporary is a
 distinct variable: each call of the factory binds its own n, and the method's
 n is untouched (it used to be aliased and overwritten: #(10 10 10))."
Object subclass: #F.
F >> test
  | n adder a5 a10 |
  n := 1.
  [n] value.
  adder := [:n | [:x | x + n]].
  a5 := adder value: 5.
  a10 := adder value: 10.
  ^ { a5 value: 0. a10 value: 0. n }

F new test
