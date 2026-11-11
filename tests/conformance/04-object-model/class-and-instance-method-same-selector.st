"XFAIL: EXPECT: instance class instance class instance class"
"§4.7 + D5, KNOWN_ISSUES K14 (open): one selector defined on the instance
 side and on the class side of a class must keep both methods, in either
 order of definition, and a class-side method of a subclass must not hide
 an instance method the subclass inherits. Today both sides write one slot
 of the class object, so the later definition replaces the earlier one
 (Foo: instances get doesNotUnderstand; Bar: the class-side method is lost
 silently), and the engine's SEND filter answers doesNotUnderstand instead
 of continuing the lookup (Qux). Remove XFAIL when the C++ fix lands."
Object subclass: #K14Foo.
K14Foo >> x
  ^ 'instance'.
K14Foo class >> x
  ^ 'class'.
Object subclass: #K14Bar.
K14Bar class >> x
  ^ 'class'.
K14Bar >> x
  ^ 'instance'.
Object subclass: #K14Baz.
K14Baz >> y
  ^ 'instance'.
K14Baz subclass: #K14Qux.
K14Qux class >> y
  ^ 'class'.
| r |
r := [:blk | [blk value] on: Error do: [:e | 'DNU']].
(r value: [K14Foo new x]) , ' ' , (r value: [K14Foo x]) , ' '
  , (r value: [K14Bar new x]) , ' ' , (r value: [K14Bar x]) , ' '
  , (r value: [K14Qux new y]) , ' ' , (r value: [K14Qux y]).
