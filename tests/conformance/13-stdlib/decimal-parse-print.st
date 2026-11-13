"EXPECT: 1.20 -0 1E-7 -1.5E+30 0.000001 1E+2 123 0E-10 -0.00 | 1E+5 0.5 7 1.0E-7 1E-7 1 -0.0 1.23456 12.3400 1E+999999999999 -0E+3 1.23456789012345678901234567895E-12 1.5E+29 0.005"
"§13 stdlib: Decimal fromString: then printString is Python's str(Decimal), the
 to-scientific-string (ProtoBase format spec, 'Decimal text'). The first group
 round-trips unchanged; the second normalises as Python does. Every expected
 string was copied from python3 -c 'from decimal import Decimal; print(str(Decimal(s)))'."
| m texts out |
m := Import from: 'decimal'.
texts := #('1.20' '-0' '1E-7' '-1.5E+30' '0.000001' '1E+2' '123' '0E-10' '-0.00' '|'
           '1e5' '.5' '+7' '1.0E-7' '0.0000001' '1.' '-.0e-0' '123.456E-2' '00012.3400'
           '1E+999999999999' '-0E+3' '12345678901234567890123456789.5E-40' '1.5e29' '5E-3').
out := WriteStream on: String new.
texts do: [:t |
    out nextPutAll: (t = '|' ifTrue: [t] ifFalse: [(m Decimal fromString: t) printString])]
  separatedBy: [out nextPut: $ ].
out contents.
