Object subclass: #Point3
  instanceVariableNames: 'x y z'.

Point3 >> setX: ax y: ay
  x := ax.
  y := ay.
  z := 0.

Point3 >> sum
  ^ x + y + z.

(Point3 new setX: 1 y: 2) sum printNl.
