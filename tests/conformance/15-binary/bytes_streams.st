"EXPECT: #[1 2 3] #[1 2 3 4 5 6] #[1 2 3] 6 #[1 2] #[3 4 5] false #[6] true"
"A byte WriteStream accumulates privately: a ByteArray it answered never changes."
s := WriteStream on: ByteArray new.
s nextPut: 1; nextPut: 2; nextPutAll: #[3].
b1 := s contents.
s nextPutAll: #[4 5]; nextPut: 6.
b2 := s contents.
r := b2 readStream.
a := r next: 2.
c := r next: 3.
e1 := r atEnd.
d := r next: 5.
b1 printString, ' ', b2 printString, ' ', b1 printString, ' ', s size printString, ' ',
  a printString, ' ', c printString, ' ', e1 printString, ' ', d printString, ' ', r atEnd printString.
