"EXPECT: 5"
"§3.2 (S37): an instance method of a subclass of an imported class assigns a
class variable the imported class declares; the write reaches the declaring
class's one slot, which the module's own method reads."
m := Import from: '_late_base_mod'.
LateBase subclass: #Kid.

Kid >> setShared
  shared := 5.

Kid new setShared.
LateBase new shared.
