"EXPECT: 10"
"-- 03_class_side_methods.st -----------------------------------------------"
"-- Class-side methods act as custom constructors: `Counter startingAt: n`  --"
"-- is sent to the class object, allocates an instance with `self new` and  --"
"-- returns it ready to use. Class-side methods are NOT visible on          --"
"-- instances (STATUS.md D5).                                               --"
"-- Expected output: 10                                                     --"

Object subclass: #Counter instanceVariableNames: 'value'.

Counter >> setValue: n
  value := n.
  ^ self.

Counter >> value
  ^ value.

Counter class >> startingAt: n
  | c |
  c := self new.
  c setValue: n.
  ^ c.

(Counter startingAt: 10) value.
