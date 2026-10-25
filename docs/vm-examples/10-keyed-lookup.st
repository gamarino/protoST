| d |
d := Dictionary new.
d at: #k put: 42.
(d at: #k) printNl.
(d at: #missing ifAbsent: [ 0 ]) printNl.
