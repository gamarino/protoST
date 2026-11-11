"EXPECT: #('kept' 100 true true 6)"
"A receiver that is not a Boolean gets closures for the block literals of an
 inlined conditional (0.10.0). While its method runs, those closures share
 the variables of the sending method. Once the send has returned, that
 method owns its variables again: a closure the receiver kept and evaluates
 later signals an Error, instead of reading or writing copies the method no
 longer sees. A kept closure that uses none of the method's variables still
 works, as does a block held in a variable."
Object subclass: #Keeper instanceVariableNames: 'kept'.
Keeper >> and: aBlock kept := aBlock. ^ #kept.
Keeper >> runKept ^ kept value.
Object subclass: #Probe.
Probe >> keep: k | x r | x := 5. r := k and: [ x := x + 1 ]. x := 100. ^ { r. x }.
Probe >> keepPure: k ^ k and: [ 2 * 3 ].
| k probe first refused text pure |
k := Keeper new.
probe := Probe new.
first := probe keep: k.
refused := false.
text := ''.
[ k runKept ] on: Error do: [ :e | refused := true. text := e messageText ].
probe keepPure: k.
pure := k runKept.
{ first first. first last. refused. (text includesSubstring: 'after that send returned'). pure }
