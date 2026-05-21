"EXPECT: deep"
"§5.1: dispatch walks the prototype chain from the receiver upward"
Object subclass: #A.
A >> tag
  ^ 'deep'.
A subclass: #B.
B subclass: #C.
C new tag.
