"EXPECT: 2"
"§9.6: Float nan = Float nan is false (D31), so two NaN objects are two Set elements (D32)"
s := Set new.
s add: Float nan.
s add: Float nan.
s size.
