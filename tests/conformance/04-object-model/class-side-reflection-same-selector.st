"EXPECT: #(true true true false #('v') #('only' 'v') true true true false 'i' 'c' 'DNU' 'c-only')"
"§4.7, K14, D52: reflection sees the two methods of one selector apart.
 respondsTo: follows the send; selectors lists each side's own methods;
 canUnderstand: asks about the receiver's instances (a class's instances, a
 metaclass's single instance, the class); perform: dispatches as a send, so
 an instance cannot reach the class side through it either."
Object subclass: #K14R.
K14R >> v  ^ 'i'.
K14R class >> v  ^ 'c'.
K14R class >> only  ^ 'c-only'.
| r |
r := [:blk | [blk value] on: Error do: [:e | e return: 'DNU']].
{ K14R respondsTo: #v. K14R new respondsTo: #v. K14R respondsTo: #only.
  K14R new respondsTo: #only.
  K14R selectors asArray. K14R class selectors asArray asSortedCollection asArray.
  K14R canUnderstand: #v. K14R class canUnderstand: #v. K14R class canUnderstand: #only.
  K14R canUnderstand: #only.
  K14R new perform: #v. K14R perform: #v.
  r value: [K14R new perform: #only].
  K14R perform: #only }
