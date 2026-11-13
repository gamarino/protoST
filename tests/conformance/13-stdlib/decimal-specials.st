"EXPECT: NaN123:n0q123 sNaN:n0s0 -Infinity:i1 -NaN:n1q0 Infinity:i0 NaN123:n0q123 -sNaN5:n1s5 Infinity:i0 -1.5E+30:f1"
"§13 stdlib: Decimal special values. Each item is the printString, then the
 kind (n NaN, i infinite, f finite), the sign, and for a NaN q (quiet) or
 s (signaling) and the payload. NaN0123 keeps payload 123 and Inf prints as
 Infinity, as in Python. The last three use the class-side constructors."
| m D describe values out |
m := Import from: 'decimal'.
D := m Decimal.
describe := [:d |
    | s |
    s := d printString , ':'.
    d isNaN ifTrue: [
        s := s , 'n' , d sign printString , (d isSignaling ifTrue: ['s'] ifFalse: ['q']) , d payload printString.
        (d isFinite or: [d isInfinite or: [d isZero]]) ifTrue: [s := s , '!'] ].
    d isInfinite ifTrue: [
        s := s , 'i' , d sign printString.
        (d isFinite or: [d isNaN or: [d isSignaling or: [d isZero]]]) ifTrue: [s := s , '!'] ].
    d isFinite ifTrue: [
        s := s , 'f' , d sign printString.
        (d isNaN or: [d isInfinite]) ifTrue: [s := s , '!'] ].
    s ].
values := OrderedCollection new.
#('NaN123' 'sNaN' '-Infinity' '-NaN' 'Inf' 'nan0123') do: [:t | values add: (D fromString: t)].
values add: (D nan: 5 signaling: true sign: 1).
values add: (D infinity: 0).
values add: (D sign: 1 coefficient: 15 exponent: 29).
out := WriteStream on: String new.
values do: [:d | out nextPutAll: (describe value: d)] separatedBy: [out nextPut: $ ].
out contents.
