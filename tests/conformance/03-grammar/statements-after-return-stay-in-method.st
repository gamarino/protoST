"EXPECT: done"
"Indented statements after a ^ belong to the method (dead code, as in Pharo);
 they do not run at top level."
Object subclass: #K.
K >> f
  ^ 1.
  Transcript showCr: 'should never print'.

K new f.
'done'
