| later |
later := [ 'computed on demand' ].
later value printNl.
#(1 2 3) doYielding: [ :x | x printNl ].
