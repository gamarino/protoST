"EXPECT-ERROR: error: NegativeRadius: circleArea: the radius -1.000000 is negative"
"A module's error no handler selects leaves the program as the same
 ProtoException; an on: Error do: does not select it (it is not an Error)."
Geo := Import from: 'example/geometry'.
[Geo circleArea(-1)] on: Error do: [:e | 'not reached'].
'not reached' printNl.
