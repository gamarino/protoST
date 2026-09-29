"EXPECT: 7"
"Instance variables are visible to subclass methods at any depth."
Object subclass: #A instanceVariableNames: 'x'.
A subclass: #B instanceVariableNames: 'y'.
B subclass: #C instanceVariableNames: 'z'.
C >> setX: v
  x := v.
  ^ self.

C >> getX
  ^ x.

(C new setX: 7) getX.
