"EXPECT: #(7 7)"
"A handler block with no argument is evaluated without the exception (cull:), as in Pharo."
{ [1/0] on: ZeroDivide do: [7]. [Error signal: 'x'] on: Error do: [:e | 7] }
