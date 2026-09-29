"EXPECT: #('ZeroDivide' 'caught MyError: custom' 'caught through whenAll:')"
"An exception unhandled in an actor method rejects the message's Future with
 the exception itself: wait re-signals it in the waiter, so a handler for its
 class (or a superclass) catches it, with its messageText intact."
Error subclass: #MyError.
Object subclass: #Worker.
Worker >> divide  ^ 1 / 0.
Worker >> custom  ^ MyError new signal: 'custom'.

| w out |
w := Worker new asActor.
out := OrderedCollection new.
out add: ([w divide wait] on: ZeroDivide do: [:e | e class name]).
out add: ([w custom wait] on: MyError do: [:e | 'caught MyError: ' , e messageText]).
out add: ([(Future whenAll: {w divide}) wait] on: ZeroDivide do: [:e | 'caught through whenAll:']).
out asArray
