"EXPECT: 3"
"§3.2 (S37): a class-side method of a subclass of an imported class names a
class-instance variable the imported class declares; the subclass has its own
slot, shared with the inherited class-side methods run on it."
m := Import from: '_late_base_mod'.
LateBase subclass: #Kid.

Kid class >> addTwo
  made := (made ifNil: [ 0 ]) + 2.
  ^ made.

LateBase noteMade.
LateBase noteMade.
Kid noteMade.
Kid addTwo.
