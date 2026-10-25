| cond blk |
cond := 3 > 2.
(cond ifTrue: [ 'inlined' ]) printNl.
blk := [ 'sent' ].
(cond ifTrue: blk) printNl.
