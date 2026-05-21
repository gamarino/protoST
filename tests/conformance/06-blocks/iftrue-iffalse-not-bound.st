"XFAIL: EXPECT: yes"
"§6.4 + D9: ifTrue:ifFalse: is not bound. Spec-correct: a two-armed conditional
should yield 'yes' when the receiver is true."
(3 > 0) ifTrue: [ 'yes' ] ifFalse: [ 'no' ].
