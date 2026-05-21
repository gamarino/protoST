"EXPECT: middle"
"§5.1: the first prototype that defines the selector wins"
Object subclass: #A.
A >> tag
  ^ 'top'.
A subclass: #B.
B >> tag
  ^ 'middle'.
B subclass: #C.
C new tag.
