"EXPECT: bad input"
"Object>>error: signals an Error carrying the text."
[Object new error: 'bad input'] on: Error do: [:e | e messageText].
