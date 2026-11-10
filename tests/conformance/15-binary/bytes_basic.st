"EXPECT: 3 255 #[1 0 255] true #[0 255]"
b := ByteArray withAll: #(1 0 255).
b size printString, ' ', (b at: 3) printString, ' ', b printString, ' ',
  (b = #[1 0 255]) printString, ' ', (b copyFrom: 2 to: 3) printString.
