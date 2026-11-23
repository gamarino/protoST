"EXPECT: #(false true false)"
"Spec §6.8, conformance 99: on a plain variable set means not nil:
 false when never assigned, true after x := 1, and false again after
 x := nil, exactly like a never-assigned variable."
Object subclass: #Point3 instanceVariableNames: 'x'.
Point3 >> setX: v
  x := v

| o |
o := Point3 basicNew.
{o instVarIsSet: #x. (o setX: 1; instVarIsSet: #x). (o setX: nil; instVarIsSet: #x)}
