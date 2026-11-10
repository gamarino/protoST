"EXPECT: 10 20 2 #[30] true nil 1 #[20 30]"
"ReadStream over a ByteArray: next, next:, position, skip:, atEnd."
r := #[10 20 30] readStream.
x := r next.
y := r next.
p := r position.
z := r next: 5.
t := r atEnd.
n := r next.
r skip: -2.
q := r position.
w := r upToEnd.
x printString, ' ', y printString, ' ', p printString, ' ', z printString, ' ', t printString, ' ',
  n printString, ' ', q printString, ' ', w printString.
