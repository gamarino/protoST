"EXPECT: TaggedAtom fresh 7"
"§10.8 (S38): `on:` is an instance-creation message of the receiver, as new
is: sent to a subclass of Atom it builds an instance of that subclass, with
initialize run as new runs it, and then stores the initial value in the
cell."
Atom subclass: #TaggedAtom instanceVariableNames: 'tag'.

TaggedAtom >> initialize
  tag := 'fresh'.

TaggedAtom >> tag
  ^ tag.

a := TaggedAtom on: 7.
a class name , ' ' , a tag , ' ' , a value printString.
