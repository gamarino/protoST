"EXPECT: 13"
"§6.1 + D29: a block created in a to:do: body is stored in a variable and"
"evaluated after the loop has finished."
b := nil.
1 to: 3 do: [ :i | b := [ :k | k + 10 ] ].
b value: 3.
