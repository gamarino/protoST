"EXPECT: #('last' 'empty' 2)"
"whenAny: answers the first result; when every future is rejected it is
 rejected with the last rejection (it used to wait forever), and whenAny: of
 an empty collection is rejected at once."
| f1 f2 any out |
f1 := Future new.
f2 := Future new.
any := Future whenAny: {f1. f2}.
f1 rejectWith: (Error new messageText: 'first').
f2 rejectWith: (Error new messageText: 'last').
out := OrderedCollection new.
out add: ([any wait] on: Error do: [:e | e messageText]).
out add: ([(Future whenAny: #()) wait] on: Error do: [:e | (e messageText includesSubstring: 'empty') ifTrue: ['empty'] ifFalse: [e messageText]]).
f1 := Future new. f2 := Future new.
any := f1 | f2.
f1 rejectWith: 'boom'.
f2 resolve: 2.
out add: any wait.
out asArray
