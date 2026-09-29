"EXPECT: é 1 a+b a b|c+d"
"Percent-decoding yields UTF-8 characters, and '+' means a space only in a
 query string."
Import from: 'http'.
d := HTTP percentDecode: '%C3%A9'.
q := HTTP parseQuery: 'x=a+b&y=c%2Bd'.
d , ' ' , d size printString , ' ' , (HTTP percentDecode: 'a+b') , ' ' , (q at: 'x') , '|' , (q at: 'y').
