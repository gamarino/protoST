"EXPECT: #('Stack' 'MySet' 'MyBag' 'MyDict' 'MyArr' 'MyArr' 'Stack' 2 1 true)"
"new / new: / withAll: on a user subclass of a built-in collection answer an
 instance of the subclass, which understands the subclass's methods and has
 received initialize."
OrderedCollection subclass: #Stack instanceVariableNames: 'pushes'.
Stack >> initialize  pushes := 0.
Stack >> push: x  pushes := pushes + 1. ^ self addLast: x.
Stack >> pop  ^ self removeLast.
Stack >> pushes  ^ pushes.
Set subclass: #MySet.
Bag subclass: #MyBag.
Dictionary subclass: #MyDict.
Array subclass: #MyArr.

| s |
s := Stack new.
s push: 1; push: 2.
{ s class name. MySet new class name. MyBag new class name. MyDict new class name.
  (MyArr new: 3) class name. (MyArr withAll: #(1 2)) class name.
  (Stack withAll: #(5 6)) class name.
  s pushes. (s pop; yourself) size. (MySet new add: 3; add: 3; yourself) size = 1 }
